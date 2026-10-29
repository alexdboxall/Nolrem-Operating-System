#include "vga16.h"
#include "../../api.h"
#include <arch.h>
#include <vmm.h>
#include <vfs.h>
#include <errno.h>
#include <string.h>
#include "../../bitmap_generic/bmp.h"
#include "vga16bmp.h"

// ---------------------------------------------------------------------
// Storage layout
//
// compat_bitmap::data doesn't point straight at the plane bytes - it
// points at this small, always-resident compat_bitmap_storage_t. It is
// also the context handed to the VMM (virt_loader::context_p), so the
// page reloader finds everything it needs here.
//
//   - `planes`: the actual pixel data. A file-backed VMM region
//     (AllocFileMemory with BitmapReload as its reload_func). It is NEVER
//     filled in by VGACreateCompatibleBitmap - each page is generated on
//     demand by the page-fault handler the first time it is touched, and
//     again after it has been discarded. The VMM stores which page is
//     which: page_origin::file_offset is the byte offset into the plane
//     data (the region is created with file_offset 0 and the VMM adds
//     i * PAGE_SIZE per page). Nothing in this file reads or writes the
//     planes directly.
//
//   - `src`: the source .bmp file, mapped read-only through the VMM as an
//     ordinary demand-paged, discardable file region. It costs no
//     physical memory until touched, and it stays at ONE fixed virtual
//     address for the life of the bitmap - which matters, because the
//     reload index below may hold positions inside the stream that are
//     only meaningful against that mapping. The VMM's page origins hold
//     the references that keep the underlying struct file alive, so the
//     bitmap doesn't store a file pointer of its own.
//
//   - `reload`: a small non-purgeable index that makes regenerating an
//     RLE-backed page cheap (see BuildReloadIndex). NULL for a non-RLE
//     source, which doesn't need one, and also NULL if it couldn't be
//     allocated - regeneration still works without it, just slower.
//     Plain AllocHeap(), since it has to survive exactly the kind of
//     discard it exists to speed up.
// ---------------------------------------------------------------------

// Frees the bitmap's storage. Order matters: the planes go first, since
// releasing them can still call back into the reloader context (this
// struct); then the source mapping; then the bookkeeping itself.
static void FreeStorage(compat_bitmap_storage_t* storage) {
    if (!storage) return;
    if (storage->planes) FreeMemory(storage->planes, storage->total_len);
    if (storage->src)    FreeMemory(storage->src, storage->src_size);
    if (storage->reload) {
        if (storage->reload->state) FreeHeap(storage->reload->state);
        FreeHeap(storage->reload);
    }
    FreeHeap(storage);
}

// ---------------------------------------------------------------------
// Shared helpers
// ---------------------------------------------------------------------

static void ComputePlaneDimensions(int width, int height, int* plane_stride,
                                    size_t* plane_size, size_t* total_len) {
    *plane_stride = (width + 7) / 8;
    *plane_size = (size_t)(*plane_stride) * (size_t)height;
    *total_len = (*plane_size) * 4;
}

// Nearest-neighbour forward mapping, used for columns (always) and for
// rows of non-RLE sources (which have true random access).
static int SourceCoordForOutput(int out_coord, int src_size, int dst_size) {
    return (out_coord * src_size) / dst_size;
}

// Closed-form inverse of SourceCoordForOutput for rows: the inclusive
// range of out_row for which SourceCoordForOutput(out_row, src_height,
// dst_height) == logical_row. This is what lets a forward-only,
// one-row-at-a-time RLE walk still serve nearest-neighbour scaling.
// May come back empty (min > max) when downscaling - that source row
// simply isn't sampled by any output row.
static void OutputRowRangeForLogicalRow(int logical_row, int src_height, int dst_height,
                                         int* out_row_min, int* out_row_max) {
    int lo = (logical_row * dst_height + src_height - 1) / src_height;
    int hi = ((logical_row + 1) * dst_height + src_height - 1) / src_height - 1;
    if (lo < 0) lo = 0;
    if (hi > dst_height - 1) hi = dst_height - 1;
    *out_row_min = lo;
    *out_row_max = hi;
}

// RLE sources are ALWAYS stored bottom-up (BmpParse rejects anything
// else), so native row N is logical row (src_height - 1 - N). Given an
// inclusive range of output rows, this gives the inclusive range of
// native rows that feed them. Note the inversion: the LOWEST output row
// needs the HIGHEST native row, and vice versa - decode order and
// output order run in opposite directions.
static void RleNativeRowRange(int out_row_lo, int out_row_hi, int src_height, int dst_height,
                               int* native_lo, int* native_hi) {
    int logical_lo = SourceCoordForOutput(out_row_lo, src_height, dst_height);
    int logical_hi = SourceCoordForOutput(out_row_hi, src_height, dst_height);
    *native_lo = src_height - 1 - logical_hi;
    *native_hi = src_height - 1 - logical_lo;
}

// Given a byte range within the compat_bitmap's plane data - which may
// span one plane or straddle a boundary between two (or more, for tiny
// images) - works out the inclusive range of OUTPUT rows it touches.
// Returns 0 if the range doesn't touch any real data.
static int FindTouchedOutputRows(size_t range_start, size_t range_size,
                                  int plane_stride, size_t plane_size, int target_height,
                                  int* out_row_lo, int* out_row_hi) {
    int touched = 0;
    for (int p = 0; p < 4; p++) {
        size_t plane_start = (size_t)p * plane_size;
        size_t plane_end = plane_start + plane_size;
        size_t lo = (range_start > plane_start) ? range_start : plane_start;
        size_t hi_excl = (range_start + range_size < plane_end) ? (range_start + range_size) : plane_end;
        if (lo >= hi_excl) continue;

        int row_lo = (int)((lo - plane_start) / plane_stride);
        int row_hi = (int)((hi_excl - 1 - plane_start) / plane_stride);
        if (row_hi >= target_height) row_hi = target_height - 1;
        if (!touched || row_lo < *out_row_lo) *out_row_lo = row_lo;
        if (!touched || row_hi > *out_row_hi) *out_row_hi = row_hi;
        touched = 1;
    }
    return touched;
}

// One-entry colour cache: adjacent pixels are very often identical
// (flat UI art, indexed sources, runs in photos), so this skips most
// repeat GetDither() calls without needing a real palette.
typedef struct {
    uint32_t colour;
    uint8_t tile[64];
    int valid;
} dither_cache_t;

static uint8_t DitherIndexFor(dither_cache_t* cache, uint32_t colour, int out_row, int col) {
    if (!cache->valid || colour != cache->colour) {
        GetDither(colour, cache->tile);
        cache->colour = colour;
        cache->valid = 1;
    }
    return cache->tile[(out_row & 7) * 8 + (col & 7)];
}

// The shared inner kernel: builds ONE byte of ONE plane - 8 horizontal
// output pixels starting at byte_off * 8 - given the native source row
// that feeds it (row_ptr, `info->bpp`-packed, readable by
// BmpDecodePixel - which is also true of a bmp_row_cursor_t row, since
// RLE rows come back normalized to 8bpp).
static uint8_t BuildPlaneByte(const bmp_info_t* info, const uint8_t* row_ptr,
                               int out_row, int byte_off, int plane, int target_width,
                               dither_cache_t* cache) {
    uint8_t result = 0;
    int col_start = byte_off * 8;
    for (int b = 0; b < 8; b++) {
        int col = col_start + b;
        if (col >= target_width) break;
        int src_col = SourceCoordForOutput(col, info->width, target_width);
        uint32_t colour = BmpDecodePixel(row_ptr, src_col, info->bpp, info->palette,
                                          info->r_mask, info->g_mask, info->b_mask);
        uint8_t index = DitherIndexFor(cache, colour, out_row, col);
        if (index & (1 << plane)) result |= (uint8_t)(0x80 >> b);
    }
    return result;
}

// ---------------------------------------------------------------------
// BuildReloadIndex (RLE sources only)
//
// RLE can't be randomly accessed: reaching row N means decoding
// everything before it. And since RLE is stored bottom-up but the
// output is top-down, a page near the TOP of the image needs a row from
// near the END of the stream - so without help, regenerating it means
// walking nearly the whole stream from the start, every time.
//
// This does one cheap pass over the RLE stream (decoding rows only -
// no dithering, no plane writes, no pixel work at all) and records, for
// each PAGE_SIZE chunk of the plane data, the decode state at the first
// native row that chunk needs. Regenerating a page then resumes right
// there and only decodes that page's own rows.
//
// `info` must have come from parsing storage->src: the checkpoints are
// only valid against that mapping, which is why the mapping is kept at a
// fixed address for the bitmap's whole life.
//
// Best effort: if anything can't be allocated, storage->reload just
// stays NULL, creation still succeeds, and regeneration falls back to
// decoding from the start of the stream.
// ---------------------------------------------------------------------

static void BuildReloadIndex(compat_bitmap_storage_t* storage, const bmp_info_t* info,
                              int plane_stride, size_t plane_size, size_t total_len,
                              int target_height) {
    int num_pages = (int)((total_len + PAGE_SIZE - 1) / PAGE_SIZE);
    if (num_pages <= 0) return;

    compat_bitmap_reload_t* reload = (compat_bitmap_reload_t*)AllocHeap(sizeof(compat_bitmap_reload_t));
    if (!reload) return;

    reload->state = (bmp_row_checkpoint_t*)AllocHeap(sizeof(bmp_row_checkpoint_t) * (size_t)num_pages);
    if (!reload->state) {
        FreeHeap(reload);
        return;
    }
    // Zeroed = "resume from the very start": safe for any slot that
    // somehow never gets a real checkpoint below.
    memset(reload->state, 0, sizeof(bmp_row_checkpoint_t) * (size_t)num_pages);
    reload->num_checkpoints = num_pages;

    // Scratch: the first native row each page needs.
    int* page_native_row_lo = (int*)AllocHeap(sizeof(int) * (size_t)num_pages);
    if (!page_native_row_lo) {
        FreeHeap(reload->state);
        FreeHeap(reload);
        return;
    }
    for (int i = 0; i < num_pages; i++) {
        int out_row_lo, out_row_hi, native_lo, native_hi;
        if (FindTouchedOutputRows((size_t)i * PAGE_SIZE, PAGE_SIZE, plane_stride, plane_size,
                                   target_height, &out_row_lo, &out_row_hi)) {
            RleNativeRowRange(out_row_lo, out_row_hi, info->height, target_height, &native_lo, &native_hi);
            page_native_row_lo[i] = native_lo;
        } else {
            page_native_row_lo[i] = -1; // never matches below
        }
    }

    bmp_row_cursor_t cursor;
    if (BmpRowCursorInit(info, &cursor) != 0) {
        FreeHeap(page_native_row_lo);
        FreeHeap(reload->state);
        FreeHeap(reload);
        return;
    }

    for (int native_row = 0; native_row < info->height; native_row++) {
        // Capture BEFORE decoding this row: a checkpoint is "about to
        // produce native_row", which is what BmpRowCursorResume() hands back first.
        for (int i = 0; i < num_pages; i++) {
            if (page_native_row_lo[i] == native_row) {
                BmpRowCursorSaveCheckpoint(&cursor, &reload->state[i]);
            }
        }
        if (BmpRowCursorNext(&cursor) == NULL) break; // shouldn't happen - defensive only
    }

    BmpRowCursorFree(&cursor);
    FreeHeap(page_native_row_lo);
    storage->reload = reload;
}

// ---------------------------------------------------------------------
// Regeneration - one fragment (the part of the request that falls in a
// single plane) at a time.
//
// A request is split per plane first because a page may straddle two
// planes, and the two halves need very different parts of an RLE source
// (the bottom rows of one plane vs. the top rows of the next) - handling
// them together would mean decoding everything in between and throwing
// it away.
// ---------------------------------------------------------------------

// Non-RLE: true random access - decode exactly the source pixels the
// requested bytes depend on, nothing else.
static void RegenRandomAccessFragment(const bmp_info_t* info, int target_width, int target_height,
                                       int plane_stride, int plane, size_t plane_start,
                                       size_t frag_start, size_t frag_end,
                                       size_t regen_offset, uint8_t* output) {
    dither_cache_t cache;
    cache.valid = 0;

    for (size_t abs = frag_start; abs < frag_end; abs++) {
        size_t offset_in_plane = abs - plane_start;
        int out_row = (int)(offset_in_plane / plane_stride);
        int byte_off = (int)(offset_in_plane % plane_stride);

        int logical_row = SourceCoordForOutput(out_row, info->height, target_height);
        int native_row = info->top_down ? logical_row : (info->height - 1 - logical_row);
        const uint8_t* row_ptr = info->pixels + (size_t)native_row * info->row_stride;

        output[abs - regen_offset] = BuildPlaneByte(info, row_ptr, out_row, byte_off, plane,
                                                     target_width, &cache);
    }
}

// RLE: stream the source forward from the best checkpoint available,
// writing out only the bytes that fall inside the requested fragment.
static void RegenRleFragment(const bmp_info_t* info, const compat_bitmap_reload_t* reload,
                              int target_width, int target_height, int plane_stride,
                              int plane, size_t plane_start,
                              size_t frag_start, size_t frag_end,
                              size_t regen_offset, uint8_t* output) {
    int out_row_lo = (int)((frag_start - plane_start) / plane_stride);
    int out_row_hi = (int)((frag_end - 1 - plane_start) / plane_stride);

    int native_lo, native_hi;
    RleNativeRowRange(out_row_lo, out_row_hi, info->height, target_height, &native_lo, &native_hi);

    // The checkpoint with the highest next_row that doesn't overshoot
    // native_lo. (Not simply "the checkpoint for the page being
    // loaded": a fragment may start partway through a page's rows, or be
    // the second half of a page that straddles a plane boundary.)
    const bmp_row_checkpoint_t* best = NULL;
    if (reload) {
        for (int i = 0; i < reload->num_checkpoints; i++) {
            const bmp_row_checkpoint_t* c = &reload->state[i];
            if (c->next_row <= native_lo && (!best || c->next_row > best->next_row)) best = c;
        }
    }

    bmp_row_cursor_t cursor;
    int start_row = 0;
    if (best) {
        if (BmpRowCursorResume(info, best, &cursor) != 0) return;
        start_row = best->next_row;
    } else {
        if (BmpRowCursorInit(info, &cursor) != 0) return;
    }

    dither_cache_t cache;
    cache.valid = 0;

    for (int native_row = start_row; native_row <= native_hi; native_row++) {
        const uint8_t* row_ptr = BmpRowCursorNext(&cursor);
        if (!row_ptr) break;
        if (native_row < native_lo) continue; // decoded only to advance the stream

        int logical_row = info->height - 1 - native_row;
        int row_min, row_max;
        OutputRowRangeForLogicalRow(logical_row, info->height, target_height, &row_min, &row_max);
        if (row_min < out_row_lo) row_min = out_row_lo;
        if (row_max > out_row_hi) row_max = out_row_hi;

        for (int out_row = row_min; out_row <= row_max; out_row++) {
            size_t row_start = plane_start + (size_t)out_row * plane_stride;
            size_t row_end = row_start + plane_stride;
            size_t lo = (frag_start > row_start) ? frag_start : row_start;
            size_t hi = (frag_end < row_end) ? frag_end : row_end;

            for (size_t abs = lo; abs < hi; abs++) {
                output[abs - regen_offset] = BuildPlaneByte(info, row_ptr, out_row,
                                                             (int)(abs - row_start), plane,
                                                             target_width, &cache);
            }
        }
    }

    BmpRowCursorFree(&cursor);
}

// ---------------------------------------------------------------------
// RegenerateCore
//
// Fills `output` with bytes [regen_offset, regen_offset + regen_size) of
// the plane data, recomputed from the in-memory .bmp at `file_data`.
// Any part of the range past the end of the real data is returned
// zeroed. Needs nothing from storage->planes, so it works identically
// for a first load and for recovery after a discard.
//
// Safe to run concurrently for different pages of the same bitmap: the
// storage, the source mapping and the reload index are only read, and
// everything mutable (bmp_info_t, cursor, dither cache) is on the stack.
// ---------------------------------------------------------------------

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wcast-align"
static void RegenerateCore(uint8_t* file_data, int target_width, int target_height,
                           const compat_bitmap_reload_t* reload,
                           size_t regen_offset, size_t regen_size, uint8_t* output) {
    memset(output, 0, regen_size); // anything not written below (e.g. past the end) stays zero

    if (!file_data || regen_size == 0) return;
    if (target_width <= 0 || target_height <= 0) return;

    bmp_info_t info;
    if (BmpParse(file_data, &info) != 0) return;

    int plane_stride;
    size_t plane_size, total_len;
    ComputePlaneDimensions(target_width, target_height, &plane_stride, &plane_size, &total_len);

    int is_rle = (info.compression == BMP_COMPRESSION_RLE8 || info.compression == BMP_COMPRESSION_RLE4);
    size_t request_end = regen_offset + regen_size;

    for (int plane = 0; plane < 4; plane++) {
        size_t plane_start = (size_t)plane * plane_size;
        size_t plane_end = plane_start + plane_size;
        size_t frag_start = (regen_offset > plane_start) ? regen_offset : plane_start;
        size_t frag_end = (request_end < plane_end) ? request_end : plane_end;
        if (frag_start >= frag_end) continue; // request doesn't touch this plane

        if (is_rle) {
            RegenRleFragment(&info, reload, target_width, target_height, plane_stride,
                             plane, plane_start, frag_start, frag_end, regen_offset, output);
        } else {
            RegenRandomAccessFragment(&info, target_width, target_height, plane_stride,
                                      plane, plane_start, frag_start, frag_end, regen_offset, output);
        }
    }
}
#pragma GCC diagnostic pop

// ---------------------------------------------------------------------
// BitmapReload - the VMM's reload_func for the plane region.
//
// Called by the page-fault handler (under the page origin's mutex, no
// spinlocks held, so it may block and may itself take page faults - the
// reads from storage->src do exactly that) whenever a page of the planes
// is touched for the first time or after it was discarded. `frame` is
// uninitialised physical memory, so the WHOLE page is written, tail
// included.
//
// Which page this is comes from the VMM, not from us: the region was
// created with file_offset 0, so po->file_offset is this page's byte
// offset into the plane data.
//
// Concurrent calls for different pages of one bitmap are fine (see
// RegenerateCore). `vr` is the per-origin copy of the loader; its
// context_p is the (shared, read-only here) compat_bitmap_storage_t.
// ---------------------------------------------------------------------

static int BitmapReload(size_t frame, struct virt_loader* vr, struct page_origin* po) {
    compat_bitmap_storage_t* storage = (compat_bitmap_storage_t*)vr->context_p;
    if (!storage || !po->is_file) return EINVAL;

    LogPrintf("BitmapReload...\n");
    size_t offset = po->file_offset;
    size_t size = 0;
    if (offset < storage->total_len) {
        size = storage->total_len - offset;
        if (size > PAGE_SIZE) size = PAGE_SIZE;
    }

    uint8_t* out = (uint8_t*)VmmMapFrame(frame);
    if (!out) {
        LogPrintf("..!\n");
        return ENOMEM;
    }
    
    memset(out, 0, PAGE_SIZE); // RegenerateCore only clears `size` bytes
    RegenerateCore(storage->src, storage->width, storage->height, storage->reload,
                   offset, size, out);

    VmmUnmapFrame(out);
    LogPrintf("BitmapReload done...\n");
    return 0;
}

// ---------------------------------------------------------------------
// Source file helpers
// ---------------------------------------------------------------------

// BmpParse takes a flat in-memory image, so the whole file gets mapped
// through the VMM. That needs its length, which is in the BMP file header
// (bfSize, 4 bytes little-endian at offset 2). Peeks at the first page to
// read it. Returns 0 if it doesn't look like a BMP.
//
// TODO: if your VFS can stat a struct file for its size, use that instead.
static size_t ReadBmpFileSize(struct file* file) {
    uint8_t* hdr = (uint8_t*)AllocFileMemory(file, 0, PAGE_SIZE, 0, NULL);
    if (!hdr) return 0;

    size_t size = 0;
    if (hdr[0] == 'B' && hdr[1] == 'M') {
        size = (size_t)hdr[2] | ((size_t)hdr[3] << 8) | ((size_t)hdr[4] << 16) | ((size_t)hdr[5] << 24);
        if (size < 14) size = 0;
    }
    FreeMemory(hdr, PAGE_SIZE);
    return size;
}

// ---------------------------------------------------------------------
// VGACreateCompatibleBitmap
//
// Sets up a "compatible bitmap": a VGA planar bitmap, dithered and
// plane-split so drawing it later is cheap. THIS FUNCTION DOES NOT
// GENERATE ANY PIXEL DATA. It maps the source file, allocates the
// metadata and a file-backed VMM region for the planes (with
// BitmapReload as its page loader), and leaves the planes untouched -
// every page of them is generated on demand by the page-fault handler.
//
// `file` is the open .bmp. The caller keeps its own reference; the VMM
// takes its own for as long as any page of the bitmap exists.
//
// Layout of the plane data (storage->planes, bmp->len bytes):
//   4 planes, back to back: plane 0 (colour bit 0) first, ... plane 3
//   (colour bit 3) last. Each plane is `stride * height` bytes, top row
//   first, where stride = (width + 7) / 8 - one bit per pixel, bit 7 =
//   leftmost pixel of the byte.
//
// target_w/target_h optionally resize the image - pass -1 for either
// (or both) to leave that dimension at the BMP's own size. Scaling is
// nearest-neighbour with plain integer maths: no floating point, no
// filtering.
//
// For an RLE-compressed source this also does one cheap decode-only
// pass to build the reload index (see BuildReloadIndex) - the only
// work done at creation time. A non-RLE source does none at all.
// ---------------------------------------------------------------------

struct compat_bitmap* VGACreateCompatibleBitmap(struct graphics_driver*, struct file* file,
                                                 int target_w, int target_h) {
    if (!file) return NULL;
    
    size_t src_size = ReadBmpFileSize(file);
    if (src_size == 0) return NULL;

    uint8_t* src = (uint8_t*)AllocFileMemory(file, 0, src_size, 0, NULL);
    if (!src) return NULL;

    bmp_info_t info;
    if (BmpParse(src, &info) != 0) {
        FreeMemory(src, src_size);
        return NULL;
    }

    int target_width  = (target_w == -1) ? info.width  : target_w;
    int target_height = (target_h == -1) ? info.height : target_h;
    if (target_width <= 0 || target_height <= 0) {
        FreeMemory(src, src_size);
        return NULL;
    }

    int plane_stride;
    size_t plane_size, total_len;
    ComputePlaneDimensions(target_width, target_height, &plane_stride, &plane_size, &total_len);

    struct compat_bitmap* bmp = (struct compat_bitmap*)AllocHeap(sizeof(struct compat_bitmap));
    if (!bmp) {
        FreeMemory(src, src_size);
        return NULL;
    }

    compat_bitmap_storage_t* storage = (compat_bitmap_storage_t*)AllocHeap(sizeof(compat_bitmap_storage_t));
    if (!storage) {
        FreeHeap(bmp);
        FreeMemory(src, src_size);
        return NULL;
    }
    storage->reload = NULL;
    storage->planes = NULL;
    storage->src = src;             // storage owns the mapping from here on
    storage->src_size = src_size;
    storage->width = target_width;
    storage->height = target_height;
    storage->total_len = total_len;

    if (info.compression == BMP_COMPRESSION_RLE8 || info.compression == BMP_COMPRESSION_RLE4) {
        BuildReloadIndex(storage, &info, plane_stride, plane_size, total_len, target_height);
    }

    // The VMM copies this by value into every page's origin. Allocated but
    // deliberately NOT written to - each page is generated by BitmapReload
    // when it's first touched.
    struct virt_loader loader;
    loader.reload_func = BitmapReload;
    loader.unmap_func = NULL;
    loader.context_s = sizeof(*storage);
    loader.context_p = storage;

    storage->planes = (uint8_t*)AllocFileMemory(file, 0, total_len, 0, &loader);
    if (!storage->planes) {
        FreeStorage(storage);
        FreeHeap(bmp);
        return NULL;
    }

    bmp->width  = target_width;
    bmp->height = target_height;
    bmp->len    = total_len;
    bmp->data   = (uint8_t*)storage;

    return bmp;
}
