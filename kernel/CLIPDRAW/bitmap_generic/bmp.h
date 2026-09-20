#pragma once

// ---------------------------------------------------------------------
// Generic Windows BMP reading routines, shared between drivers.
//
// BmpParse() does the minimum work needed to make a BMP's pixels
// addressable: parsing the header and locating the palette/masks/pixel
// data. It does NOT decode RLE4/RLE8 up front - on a memory-tight
// system, allocating a second copy of the whole image just to make it
// randomly addressable is often not affordable, and RLE can't be
// randomly addressed without either that full decode or re-decoding
// from the start on every single access (which is far too slow).
//
// So there are two ways to read pixels back out, and which one you
// need depends on the source:
//
//   - BmpGetPixel(info, x, y) / BmpDecodePixel(row_ptr, ...): true
//     random access, for non-RLE sources only (info->pixels is NULL
//     for an RLE source - BmpGetPixel returns a sentinel colour rather
//     than reading through a null pointer if you call it on one by
//     mistake).
//
//   - bmp_row_cursor_t / BmpRowCursorInit / BmpRowCursorNext /
//     BmpRowCursorFree: forward-only, row-at-a-time access that works
//     for EVERY source (RLE or not). For RLE it decodes one row into a
//     small internal buffer (width bytes - not the whole image) and
//     advances the compressed stream exactly as far as it needs to;
//     for anything else it's just pointer arithmetic into the existing
//     file data, no extra memory at all. Rows come out in NATIVE (file
//     storage) order, not necessarily top-down - see
//     bmp_info_t::top_down - and MUST be requested in that order, one
//     at a time: the cursor can't skip forward or go backward, which
//     is what lets it avoid buffering the image. A caller that wants
//     e.g. nearest-neighbour scaling therefore has to walk source rows
//     forward and work out which output row(s) each one feeds, rather
//     than the more obvious "for each output row, fetch its source
//     row" - see VGACreateCompatibleBitmap for a worked example of the
//     inversion this needs (it's a closed-form formula, not a search).
//
// Supports: 1/4/8bpp paletted, 16bpp (555 or BI_BITFIELDS masks,
// typically 565), 24bpp, 32bpp (BGRX or BI_BITFIELDS/ALPHABITFIELDS
// masks - alpha itself is ignored), and RLE4/RLE8 compression.
// Not supported: the old 12-byte OS/2 BITMAPCOREHEADER.
//
// As throughout this codebase, the whole BMP file is assumed to
// already be loaded into memory at the pointer passed in - there's no
// length parameter, and (bar the light bounds-checking done on the RLE
// stream, using the file's own declared size fields) it trusts the
// file to be well-formed.
// ---------------------------------------------------------------------

#include <stdint.h>
#include <stddef.h>

#define BMP_COMPRESSION_RGB            0
#define BMP_COMPRESSION_RLE8           1
#define BMP_COMPRESSION_RLE4           2
#define BMP_COMPRESSION_BITFIELDS      3
#define BMP_COMPRESSION_ALPHABITFIELDS 6

typedef struct {
    int width;
    int height;

    // 1 if native row 0 is the image's TOP row, 0 if it's the BOTTOM
    // row. Always 0 for RLE sources (the BMP spec requires RLE to be
    // stored bottom-up). "Native" here means the order pixel data is
    // actually walked in by the row cursor (and, for non-RLE, the
    // order rows sit in `pixels`) - it's not necessarily top-down.
    int top_down;

    // The format pixel data is packed in, wherever you read it from:
    // 1, 4, 8, 16, 24 or 32. RLE4/RLE8 sources are normalized to 8 -
    // the row cursor always hands back one decoded index byte per
    // pixel for those, regardless of the original on-disk packing.
    uint16_t bpp;

    uint32_t compression; // BMP_COMPRESSION_* - the file's actual compression

    // Palette: BGRA quads, valid when bpp <= 8 (including the RLE
    // case, where bpp is always normalized to 8), NULL otherwise.
    // Always points directly into the source file - the palette itself
    // is never compressed, even when the pixel data is.
    const uint8_t* palette;
    uint32_t palette_count;

    // Channel masks, valid when bpp is 16 or 32, unused otherwise.
    uint32_t r_mask, g_mask, b_mask;

    // ---- non-RLE sources only ----
    // Row-major pixel data, straight from the file, `bpp`-packed, one
    // row every `row_stride` bytes. NULL when compression is RLE4/RLE8
    // - use a bmp_row_cursor_t for those (see the file header comment).
    const uint8_t* pixels;
    int row_stride;

    // ---- RLE4/RLE8 sources only ----
    // The raw compressed byte stream, straight from the file (nothing
    // decoded here), and a safe upper bound on how many bytes of it
    // belong to this image. NULL/0 for non-RLE sources. A
    // bmp_row_cursor_t reads these; there's rarely a reason to touch
    // them directly, but they're exposed in case a driver wants to
    // walk the compressed stream itself instead.
    const uint8_t* rle_data;
    uint32_t rle_len;
} bmp_info_t;

// Parses a BMP file's header (already fully loaded at bmp_file_data)
// into `out`. Allocates nothing and decodes no pixel data - see the
// file header comment for how to actually read pixels back out.
// Returns 1 on success, 0 on failure (bad signature, unsupported
// header or bit depth, or an RLE-compressed file that isn't stored
// bottom-up as the format requires).
int BmpParse(const uint8_t* bmp_file_data, bmp_info_t* out);

// True random-access pixel read at (x, y) - x in [0, width), y in
// [0, height), y = 0 is always the TOP row - returned as 0xFFRRGGBB
// (always opaque; BMP colour has no meaningful alpha here). Only valid
// for non-RLE sources (info->pixels != NULL); called on an RLE source
// it returns opaque black rather than reading through a null pointer.
uint32_t BmpGetPixel(const bmp_info_t* info, int x, int y);

// The building block behind BmpGetPixel() and bmp_row_cursor_t alike,
// for reading one pixel out of an already-available row of `bpp`-
// packed data (from bmp_info_t::pixels directly, or from a row a
// bmp_row_cursor_t just handed back). col is the x position within
// that row.
uint32_t BmpDecodePixel(const uint8_t* row_ptr, int col, uint16_t bpp,
                         const uint8_t* palette,
                         uint32_t r_mask, uint32_t g_mask, uint32_t b_mask);

// ---------------------------------------------------------------------
// bmp_row_cursor_t - forward-only, row-at-a-time pixel access that
// works for RLE and non-RLE sources alike. See the file header comment
// for why this exists and how rows are ordered.
// ---------------------------------------------------------------------
typedef struct {
    const bmp_info_t* info;

    // RLE decode state - unused (zeroed) for non-RLE sources.
    const uint8_t* rle_data;
    uint32_t rle_len;
    size_t   rle_pos;
    int      rle_x, rle_y; // current position within the RLE stream's own coordinate space
    int      finished;      // 1 once end-of-bitmap or a truncated stream has been hit
    uint8_t* row_buffer;    // owned scratch space, `width` bytes - RLE sources only; NULL otherwise

    int next_row; // which native row BmpRowCursorNext() will return next
} bmp_row_cursor_t;

// Initializes a cursor over `info` (as produced by BmpParse). For an
// RLE source this allocates one row's worth of scratch space (`width`
// bytes) via AllocHeap - NOT the whole image; for anything else it
// allocates nothing at all. Returns 1 on success, 0 on allocation
// failure.
int BmpRowCursorInit(const bmp_info_t* info, bmp_row_cursor_t* cursor);

// Returns the next native row's pixel data - `bpp`-packed, read with
// BmpDecodePixel() the same way as bmp_info_t::pixels - or NULL once
// all `height` rows have been returned. Must be called exactly
// `info->height` times, in order, with no skipping: for an RLE source
// that's what lets this avoid ever buffering more than one row, and
// for a non-RLE source it's just kept consistent so callers don't need
// to care which kind of source they have.
const uint8_t* BmpRowCursorNext(bmp_row_cursor_t* cursor);

// Releases whatever BmpRowCursorInit() allocated. Safe to call
// unconditionally.
void BmpRowCursorFree(bmp_row_cursor_t* cursor);
