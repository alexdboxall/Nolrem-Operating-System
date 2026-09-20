#include "bmp.h"
#include "../api.h"
#include <string.h>

static uint32_t ReadU32LE(const uint8_t* p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static uint16_t ReadU16LE(const uint8_t* p) {
    return (uint16_t)(p[0] | (p[1] << 8));
}

// Extracts a colour component via its bitmask and scales it to 8 bits.
// Works for any mask width up to 32 bits, so it covers both 16bpp
// (555/565) and 32bpp (BI_BITFIELDS) pixels with one routine.
static uint8_t ExpandComponent(uint32_t pixel, uint32_t mask) {
    if (mask == 0) return 0;

    int shift = 0;
    uint32_t m = mask;
    while (!(m & 1)) { m >>= 1; shift++; }

    int bits = 0;
    while (m & 1) { m >>= 1; bits++; }

    uint32_t val = (pixel & mask) >> shift;

    if (bits >= 8) return (uint8_t)(val >> (bits - 8));

    int rem = 2 * bits - 8;
    if (rem < 0) rem = 0;
    return (uint8_t)((val << (8 - bits)) | (val >> rem)); // replicate high bits
}

uint32_t BmpDecodePixel(const uint8_t* row_ptr, int col, uint16_t bpp,
                         const uint8_t* palette,
                         uint32_t r_mask, uint32_t g_mask, uint32_t b_mask) {
    uint8_t r, g, b;

    switch (bpp) {
    case 1: {
        uint8_t byte = row_ptr[col >> 3];
        uint8_t idx = (uint8_t)((byte >> (7 - (col & 7))) & 1);
        const uint8_t* e = palette + (size_t)idx * 4; // BGRA quad
        b = e[0]; g = e[1]; r = e[2];
        break;
    }
    case 4: {
        uint8_t byte = row_ptr[col >> 1];
        uint8_t idx = (col & 1) ? (byte & 0x0F) : (byte >> 4);
        const uint8_t* e = palette + (size_t)idx * 4;
        b = e[0]; g = e[1]; r = e[2];
        break;
    }
    case 8: {
        uint8_t idx = row_ptr[col];
        const uint8_t* e = palette + (size_t)idx * 4;
        b = e[0]; g = e[1]; r = e[2];
        break;
    }
    case 16: {
        uint16_t px = ReadU16LE(row_ptr + col * 2);
        r = ExpandComponent(px, r_mask);
        g = ExpandComponent(px, g_mask);
        b = ExpandComponent(px, b_mask);
        break;
    }
    case 24: {
        const uint8_t* e = row_ptr + col * 3; // stored B,G,R
        b = e[0]; g = e[1]; r = e[2];
        break;
    }
    case 32:
    default: {
        uint32_t px = ReadU32LE(row_ptr + col * 4);
        r = ExpandComponent(px, r_mask);
        g = ExpandComponent(px, g_mask);
        b = ExpandComponent(px, b_mask);
        break;
    }
    }

    return (0xFFu << 24) | ((uint32_t)r << 16) | ((uint32_t)g << 8) | b;
}

uint32_t BmpGetPixel(const bmp_info_t* info, int x, int y) {
    if (!info->pixels) return 0xFF000000; // RLE source - use a bmp_row_cursor_t instead
    int actual_row = info->top_down ? y : (info->height - 1 - y);
    const uint8_t* row_ptr = info->pixels + (size_t)actual_row * info->row_stride;
    return BmpDecodePixel(row_ptr, x, info->bpp, info->palette,
                           info->r_mask, info->g_mask, info->b_mask);
}

int BmpParse(const uint8_t* bmp_file_data, bmp_info_t* out) {
    memset(out, 0, sizeof(*out));

    const uint8_t* fh = bmp_file_data;
    if (fh[0] != 'B' || fh[1] != 'M') return 0;

    uint32_t file_size = ReadU32LE(fh + 2);
    uint32_t pixel_data_offset = ReadU32LE(fh + 10);

    const uint8_t* ih = bmp_file_data + 14;
    uint32_t header_size = ReadU32LE(ih + 0);
    if (header_size < 40) return 0; // OS/2 BITMAPCOREHEADER etc - not handled

    int32_t width      = (int32_t)ReadU32LE(ih + 4);
    int32_t height_raw = (int32_t)ReadU32LE(ih + 8);
    uint16_t bpp         = ReadU16LE(ih + 14);
    uint32_t compression = ReadU32LE(ih + 16);
    uint32_t image_size  = ReadU32LE(ih + 20);
    uint32_t colors_used = ReadU32LE(ih + 32);

    if (width <= 0) return 0;
    int top_down = (height_raw < 0);
    int height = top_down ? -height_raw : height_raw;
    if (height <= 0) return 0;

    if (compression == BMP_COMPRESSION_RLE8 || compression == BMP_COMPRESSION_RLE4) {
        if (top_down) return 0; // invalid per the BMP spec - RLE requires bottom-up storage

        uint16_t nominal_bpp = (compression == BMP_COMPRESSION_RLE8) ? 8 : 4;

        out->width = width;
        out->height = height;
        out->top_down = 0; // RLE is always bottom-up
        out->bpp = 8;       // the row cursor always yields one decoded index byte per pixel
        out->compression = compression;
        out->palette = bmp_file_data + 14 + header_size;
        out->palette_count = colors_used ? colors_used : (1u << nominal_bpp);

        // Locate the compressed stream and bound its length - nothing
        // is decoded here, that's the row cursor's job.
        out->rle_data = bmp_file_data + pixel_data_offset;
        out->rle_len = image_size;
        if (out->rle_len == 0) {
            // Not every encoder sets biSizeImage for RLE data - fall
            // back to "rest of the file" via the file's own size
            // field, so a missing/zero value doesn't turn into an
            // unbounded read later.
            out->rle_len = (file_size > pixel_data_offset) ? (file_size - pixel_data_offset) : 0;
        }
        return 1;
    }

    if (bpp != 1 && bpp != 4 && bpp != 8 && bpp != 16 && bpp != 24 && bpp != 32) {
        return 0; // unsupported bit depth
    }

    out->width = width;
    out->height = height;
    out->top_down = top_down;
    out->bpp = bpp;
    out->compression = compression;

    if (bpp <= 8) {
        out->palette = bmp_file_data + 14 + header_size; // array of BGRA quads
        out->palette_count = colors_used ? colors_used : (1u << bpp);
    } else if (bpp == 16) {
        if (compression == BMP_COMPRESSION_BITFIELDS) {
            const uint8_t* mask_ptr = (header_size >= 56) ? (ih + 40) : (ih + header_size);
            out->r_mask = ReadU32LE(mask_ptr + 0);
            out->g_mask = ReadU32LE(mask_ptr + 4);
            out->b_mask = ReadU32LE(mask_ptr + 8);
        } else {
            out->r_mask = 0x7C00; out->g_mask = 0x03E0; out->b_mask = 0x001F; // X1R5G5B5
        }
    } else if (bpp == 32) {
        if (compression == BMP_COMPRESSION_BITFIELDS || compression == BMP_COMPRESSION_ALPHABITFIELDS) {
            const uint8_t* mask_ptr = (header_size >= 56) ? (ih + 40) : (ih + header_size);
            out->r_mask = ReadU32LE(mask_ptr + 0);
            out->g_mask = ReadU32LE(mask_ptr + 4);
            out->b_mask = ReadU32LE(mask_ptr + 8);
        } else {
            out->r_mask = 0x00FF0000; out->g_mask = 0x0000FF00; out->b_mask = 0x000000FF; // BGRX
        }
    }
    // bpp == 24 needs neither - read directly, three bytes per pixel.

    out->row_stride = ((width * bpp + 31) / 32) * 4;
    out->pixels = bmp_file_data + pixel_data_offset;
    return 1;
}

// ---------------------------------------------------------------------
// bmp_row_cursor_t
//
// For non-RLE sources this is trivial - BmpRowCursorNext() is just
// pointer arithmetic into bmp_info_t::pixels, no state needed beyond a
// row counter.
//
// For RLE, BmpRowCursorNextRLE() below is the real work: a state
// machine that resumes the RLE token stream exactly where the previous
// call left off, and runs only until the CURRENT native row is
// complete (an end-of-line escape, an end-of-bitmap escape, a delta
// that moves to a different row, or the stream running out), then
// returns. Both formats stream two-byte tokens: (count, value) draws
// `count` pixels of `value`, or when count is 0 it's an escape -
// 0 = end of line, 1 = end of bitmap, 2 = delta (next two bytes are a
// (dx,dy) jump), >=3 = absolute mode (that many literal pixels follow
// directly, padded to a 2-byte boundary). RLE8's `value` is one
// literal palette index; RLE4's is two, packed as nibbles (high =
// first pixel, low = second) and used alternately.
//
// A delta with dy > 0, or an end-of-line, can leave the stream's own
// row counter (rle_y) ahead of the row this cursor is actually being
// asked for (next_row) - that just means the skipped rows had no data
// of their own, so they're handed back as all-zero (palette index 0)
// without touching the stream further, until next_row catches up.
// ---------------------------------------------------------------------

static const uint8_t* BmpRowCursorNextRLE(bmp_row_cursor_t* cursor) {
    int width = cursor->info->width;
    int target_row = cursor->next_row;

    memset(cursor->row_buffer, 0, (size_t)width); // default: palette index 0

    if (cursor->finished || target_row < cursor->rle_y) {
        cursor->next_row++;
        return cursor->row_buffer;
    }

    const uint8_t* data = cursor->rle_data;
    uint32_t len = cursor->rle_len;
    int is_rle4 = (cursor->info->compression == BMP_COMPRESSION_RLE4);

    while (!cursor->finished && cursor->rle_y == target_row) {
        if (cursor->rle_pos + 2 > len) { cursor->finished = 1; break; }

        uint8_t count  = data[cursor->rle_pos];
        uint8_t second = data[cursor->rle_pos + 1];
        cursor->rle_pos += 2;

        if (count == 0) {
            if (second == 0) { cursor->rle_x = 0; cursor->rle_y++; break; } // end of line
            if (second == 1) { cursor->finished = 1; break; }                // end of bitmap
            if (second == 2) {                                              // delta
                if (cursor->rle_pos + 2 > len) { cursor->finished = 1; break; }
                int dx = data[cursor->rle_pos++];
                int dy = data[cursor->rle_pos++];
                cursor->rle_x += dx;
                cursor->rle_y += dy;
                if (dy > 0) break; // moved to a different row - this row is done
                continue;          // stayed on the same row, keep decoding it
            }

            // absolute mode: `second` literal indices/pixels follow
            int n = second;
            int bytes_needed = is_rle4 ? (n + 1) / 2 : n;
            for (int i = 0; i < n; i++) {
                uint8_t idx;
                if (is_rle4) {
                    size_t byte_index = cursor->rle_pos + (size_t)(i / 2);
                    if (byte_index >= len) break;
                    uint8_t byte = data[byte_index];
                    idx = (i & 1) ? (byte & 0x0F) : (byte >> 4);
                } else {
                    size_t byte_index = cursor->rle_pos + (size_t)i;
                    if (byte_index >= len) break;
                    idx = data[byte_index];
                }
                if (cursor->rle_x >= 0 && cursor->rle_x < width) {
                    cursor->row_buffer[cursor->rle_x] = idx;
                }
                cursor->rle_x++;
            }
            cursor->rle_pos += (size_t)bytes_needed;
            if ((bytes_needed & 1) && cursor->rle_pos < len) cursor->rle_pos++; // word-align padding
            continue;
        }

        // encoded run of `count` pixels
        if (is_rle4) {
            uint8_t hi = second >> 4, lo = second & 0x0F;
            for (int i = 0; i < count; i++) {
                if (cursor->rle_x >= 0 && cursor->rle_x < width) {
                    cursor->row_buffer[cursor->rle_x] = (i & 1) ? lo : hi;
                }
                cursor->rle_x++;
            }
        } else {
            for (int i = 0; i < count; i++) {
                if (cursor->rle_x >= 0 && cursor->rle_x < width) {
                    cursor->row_buffer[cursor->rle_x] = second;
                }
                cursor->rle_x++;
            }
        }
    }

    cursor->next_row++;
    return cursor->row_buffer;
}

int BmpRowCursorInit(const bmp_info_t* info, bmp_row_cursor_t* cursor) {
    memset(cursor, 0, sizeof(*cursor));
    cursor->info = info;

    if (info->compression == BMP_COMPRESSION_RLE8 || info->compression == BMP_COMPRESSION_RLE4) {
        cursor->row_buffer = (uint8_t*)AllocHeap((size_t)info->width);
        if (!cursor->row_buffer) return 0;
        cursor->rle_data = info->rle_data;
        cursor->rle_len  = info->rle_len;
        // rle_pos, rle_x, rle_y, finished, next_row are already 0 via memset
    }
    // Non-RLE: nothing to allocate - row_buffer stays NULL, and
    // BmpRowCursorNext() reads straight out of info->pixels/row_stride.

    return 1;
}

const uint8_t* BmpRowCursorNext(bmp_row_cursor_t* cursor) {
    if (cursor->next_row >= cursor->info->height) return NULL; // fully drained

    if (cursor->row_buffer) {
        return BmpRowCursorNextRLE(cursor);
    }

    const uint8_t* row = cursor->info->pixels + (size_t)cursor->next_row * cursor->info->row_stride;
    cursor->next_row++;
    return row;
}

void BmpRowCursorFree(bmp_row_cursor_t* cursor) {
    if (cursor && cursor->row_buffer) {
        FreeHeap(cursor->row_buffer);
        cursor->row_buffer = NULL;
    }
}