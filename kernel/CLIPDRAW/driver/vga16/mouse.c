#include "vga16.h"

void VGADrawMouse(struct graphics_driver*, int x, int y, const uint32_t* black, const uint32_t* white, void* _restore_buffer, int width, int height) {
    volatile uint8_t* vram = VRAM_BASE;
    volatile uint8_t dummy;
    
    /* We requested MOUSE_BUFFER_UINT8 to save memory. */
    uint8_t* restore_buffer = (uint8_t*) _restore_buffer;

    int start_byte = x >> 3;
    int end_byte   = (x + width - 1) >> 3;

    // Write mode 2 (bits 0-1 = 2) and read mode 0 (bit 3 = 0) live in the
    // same GC mode register and don't conflict, so we can set this once
    // for the whole blit rather than juggling separate "read" and "write"
    // register values per byte.
    outb(VGA_GC_INDEX, 0x05);
    outb(VGA_GC_DATA, 0x02);

    for (int row = 0; row < height; row++) {
        int yy = y + row;
        uint32_t black_row = black[row];
        uint32_t white_row = white[row];

        for (int b = start_byte; b <= end_byte; b++) {
            volatile uint8_t* ptr = vram + (yy * BYTES_PER_ROW) + b;

            // --- Read pass: 4 reads (one per plane) reconstruct all 8
            // pixels in this byte column, instead of 8 pixels x 4 planes
            // = 32 reads the naive per-pixel approach would need. This
            // also primes the write-mode-2 latches with the byte's
            // current contents, ready for the write pass below - any
            // read latches all 4 planes' data for that address regardless
            // of which plane Read Map Select is pointed at.
            uint8_t plane_byte[4];
            for (int plane = 0; plane < 4; plane++) {
                outb(VGA_GC_INDEX, 0x04);
                outb(VGA_GC_DATA, plane);
                plane_byte[plane] = *ptr;
            }

            uint8_t black_mask = 0;
            uint8_t white_mask = 0;

            for (int k = 0; k < 8; k++) {
                int abs_x = b * 8 + k;
                int col = abs_x - x;
                if (col < 0 || col >= width) continue; // outside cursor bounds

                uint8_t bitmask = 0x80 >> k;

                int vga_index = 0;
                for (int plane = 0; plane < 4; plane++) {
                    if (plane_byte[plane] & bitmask) vga_index |= (1 << plane);
                }
                restore_buffer[row * width + col] = vga_index;

                uint32_t cursor_bit = 1u << (31 - col);
                if (black_row & cursor_bit)      black_mask |= bitmask;
                else if (white_row & cursor_bit) white_mask |= bitmask;
            }

            // --- Write pass: one accelerated masked write per colour
            // actually present in this byte (0, 1, or 2 writes total,
            // never per-pixel). Bits outside the mask fall through to the
            // latches - so if BOTH colours appear in the same byte, the
            // latches must be refreshed between writes, or the white
            // write's un-masked bits will pull the stale pre-write
            // background back over the black pixels just painted.
            if (black_mask) {
                outb(VGA_GC_INDEX, 0x08);
                outb(VGA_GC_DATA, black_mask);
                *ptr = 0x0; // black
            }
            if (white_mask) {
                if (black_mask) {
                    dummy = *ptr; // re-latch: pick up the black write just made
                }
                outb(VGA_GC_INDEX, 0x08);
                outb(VGA_GC_DATA, white_mask);
                *ptr = 0xF; // white
            }
        }
    }

    outb(VGA_GC_INDEX, 0x08);
    outb(VGA_GC_DATA, 0xFF);
    (void) dummy;
}

void VGARemoveMouse(struct graphics_driver*, int x, int y, void* _restore_buffer, int width, int height) {
    volatile uint8_t* vram = VRAM_BASE;
    volatile uint8_t dummy;
    
    /* We requested MOUSE_BUFFER_UINT8 to save memory. */
    uint8_t* restore_buffer = (uint8_t*) _restore_buffer;

    
}