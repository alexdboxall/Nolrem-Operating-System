#include "vga16.h"

void VGABitmapBlit(struct graphics_driver*, struct compat_bitmap* bmp,
                    struct rect src, struct point dest) {
    int src_x = src.x;
    int src_y = src.y;
    int w = src.w;
    int h = src.h;
    int dest_x = dest.x;
    int dest_y = dest.y;

    if (!bmp || !bmp->data) return;
    if (w <= 0 || h <= 0) return;

    // Clip against the source bitmap's own bounds.
    if (src_x < 0) { w += src_x; dest_x -= src_x; src_x = 0; }
    if (src_y < 0) { h += src_y; dest_y -= src_y; src_y = 0; }
    if (src_x + w > bmp->width)  w = bmp->width  - src_x;
    if (src_y + h > bmp->height) h = bmp->height - src_y;
    if (w <= 0 || h <= 0) return;

    // Clip against the screen.
    if (dest_x < 0) { w += dest_x; src_x -= dest_x; dest_x = 0; }
    if (dest_y < 0) { h += dest_y; src_y -= dest_y; dest_y = 0; }
    if (dest_x + w > 640) w = 640 - dest_x;
    if (dest_y + h > 480) h = 480 - dest_y;
    if (w <= 0 || h <= 0) return;

    volatile uint8_t* vram = VRAM_BASE;
    volatile uint8_t dummy;

    int plane_stride = (bmp->width + 7) / 8;
    size_t plane_size = (size_t)plane_stride * (size_t)bmp->height;

    outb(VGA_GC_INDEX, 0x05); outb(VGA_GC_DATA, 0x00); // write mode 0
    outb(VGA_GC_INDEX, 0x01); outb(VGA_GC_DATA, 0x00); // enable set/reset: off
    outb(VGA_GC_INDEX, 0x03); outb(VGA_GC_DATA, 0x00); // rotate 0, function none

    for (int p = 0; p < 4; p++) {
        const uint8_t* plane_data = bmp->data + (size_t)p * plane_size;

        outb(VGA_SEQ_INDEX, 0x02);
        outb(VGA_SEQ_DATA, (uint8_t)(1 << p)); // this plane only

        int current_mask = -1; // GC Bit Mask cache; -1 = "not set yet"

        for (int row = 0; row < h; row++) {
            const uint8_t* src_row = plane_data + (size_t)(src_y + row) * plane_stride;
            volatile uint8_t* dst_row = vram + (dest_y + row) * BYTES_PER_ROW;

            int col = 0;
            while (col < w) {
                int db_index = (dest_x + col) >> 3;

                uint8_t dest_byte = 0, touched_mask = 0;
                while (col < w && ((dest_x + col) >> 3) == db_index) {
                    int dcol = dest_x + col;
                    int scol = src_x + col;
                    uint8_t dbit = (uint8_t)(1 << (7 - (dcol & 7)));

                    uint8_t src_byte = src_row[scol >> 3];
                    int src_bit = (src_byte >> (7 - (scol & 7))) & 1;
                    if (src_bit) dest_byte |= dbit;
                    touched_mask |= dbit;

                    col++;
                }

                if (current_mask != touched_mask) {
                    outb(VGA_GC_INDEX, 0x08);
                    outb(VGA_GC_DATA, touched_mask);
                    current_mask = touched_mask;
                }

                volatile uint8_t* ptr = dst_row + db_index;
                dummy = *ptr;      // latch the byte's current content
                *ptr = dest_byte;  // Bit Mask keeps untouched columns as-is
            }
        }
    }

    // Restore shared register state for the rest of this file.
    outb(VGA_GC_INDEX, 0x01); outb(VGA_GC_DATA, 0x00); // enable set/reset: off (resting)
    outb(VGA_GC_INDEX, 0x08); outb(VGA_GC_DATA, 0xFF); // bit mask: all bits (resting)
    outb(VGA_SEQ_INDEX, 0x02); outb(VGA_SEQ_DATA, 0x0F); // map mask: all planes (resting)
    (void)dummy;
}