#include "vga16.h"
#include "../../api.h"
#include <string.h>
#include "../../bitmap_generic/bmp.h"

struct compat_bitmap* VGACreateCompatibleBitmap(struct graphics_driver*, uint8_t* bmp_file_data,
                                                 int target_w, int target_h) {
    bmp_info_t info;
    if (!BmpParse(bmp_file_data, &info)) return NULL;
 
    // ---- resolve target (output) dimensions ----
    int target_width  = (target_w == -1) ? info.width  : target_w;
    int target_height = (target_h == -1) ? info.height : target_h;
    if (target_width <= 0 || target_height <= 0) return NULL;
 
    // ---- allocate the compat_bitmap and its planar backing store ----
    int plane_stride = (target_width + 7) / 8;
    size_t plane_size = (size_t)plane_stride * (size_t)target_height;
    size_t total_len = plane_size * 4;
 
    LogPrintf("Allocating for the compatible bitmap... total length = 0x%X\n", total_len);
    uint8_t* data = (uint8_t*)AllocHeap(total_len);
    LogPrintf("Done alloacting compatible bitmap! It's at 0x%X\n", data);
    if (!data) return NULL;
    memset(data, 0, total_len); // planes start at 0; the loop below only sets bits
 
    struct compat_bitmap* bmp = (struct compat_bitmap*)AllocHeap(sizeof(struct compat_bitmap));
    if (!bmp) {
        FreeHeap(data);
        return NULL;
    }
 
    bmp->width  = target_width;
    bmp->height = target_height;
    bmp->len    = total_len;
    bmp->data   = data;
 
    uint8_t* plane[4];
    for (int p = 0; p < 4; p++) plane[p] = data + (size_t)p * plane_size;
 
    bmp_row_cursor_t cursor;
    if (!BmpRowCursorInit(&info, &cursor)) {
        FreeHeap(bmp);
        FreeHeap(data);
        return NULL;
    }
 
    // One-entry colour cache: adjacent pixels are very often identical
    // (flat UI art, indexed sources, runs in photos), so this alone
    // skips most repeat GetDither calls without needing a real palette.
    uint32_t cached_colour = 0;
    uint8_t  cached_tile[64];
    int      have_cache = 0;
 
    for (int native_row = 0; native_row < info.height; native_row++) {
        const uint8_t* row_ptr = BmpRowCursorNext(&cursor);
        if (!row_ptr) break; // shouldn't happen - defensive only
 
        // Which OUTPUT rows this source row feeds - zero, one, or
        // several, depending on whether we're down- or up-scaling.
        // Closed-form inverse of src_row(out_row) = out_row*height/target_height,
        // i.e. the range of out_row for which that floor division equals
        // this row's logical (top-down) index.
        int logical_row = info.top_down ? native_row : (info.height - 1 - native_row);
        int out_row_min = (logical_row * target_height + info.height - 1) / info.height;
        int out_row_max = ((logical_row + 1) * target_height + info.height - 1) / info.height - 1;
        if (out_row_min < 0) out_row_min = 0;
        if (out_row_max > target_height - 1) out_row_max = target_height - 1;
 
        if (out_row_min > out_row_max) continue; // this source row isn't used by any output row (downscale)
 
        for (int col = 0; col < target_width; col++) {
            int src_col = (col * info.width) / target_width;
            uint32_t colour = BmpDecodePixel(row_ptr, src_col, info.bpp, info.palette,
                                              info.r_mask, info.g_mask, info.b_mask);
 
            if (!have_cache || colour != cached_colour) {
                GetDither(colour, cached_tile);
                cached_colour = colour;
                have_cache = 1;
            }
 
            int byte_off = col >> 3;
            uint8_t bit = (uint8_t)(0x80 >> (col & 7));
 
            // Stamp this column into every output row that uses this
            // source row - each gets its own dither phase out of the
            // SAME tile, since GetDither only needed computing once.
            for (int out_row = out_row_min; out_row <= out_row_max; out_row++) {
                uint8_t index = cached_tile[(out_row & 7) * 8 + (col & 7)];
                for (int p = 0; p < 4; p++) {
                    if (index & (1 << p)) {
                        plane[p][(size_t)out_row * plane_stride + byte_off] |= bit;
                    }
                }
            }
        }
    }
 
    BmpRowCursorFree(&cursor);
    return bmp;
}
