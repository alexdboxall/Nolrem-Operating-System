#include "vga16.h"

void VgaInvertRect(struct graphics_driver*, int x1, int y1, int x2, int y2) {
    if (x1 >= x2 || y1 >= y2) return;
    if (x1 >= 640 || y1 >= 480) return;
    if (x1 < 0) x1 = 0;
    if (x2 > 640) x2 = 640;   // FIX: was `> 639` / `= 639`
    if (y1 < 0) y1 = 0;
    if (y2 > 480) y2 = 480;

    outb(0x3CE, 0x03);
    outb(0x3CF, 0x18);
    outb(0x3CE, 0x08);

    int byte_x1 = x1 >> 3;
    int byte_x2 = (x2 - 1) >> 3;   // FIX: was `x2 >> 3`

    uint8_t mask_start = 0xFF >> (x1 & 7);
    uint8_t mask_end   = 0xFF << (7 - ((x2 - 1) & 7));  // FIX: was `7 - (x2 & 7)`

    volatile uint8_t* screen_base = (volatile uint8_t*)0xC00A0000;

    for (int y = y1; y < y2; y++) {
        volatile uint8_t* row_ptr = screen_base + (y * 80);

        if (byte_x1 == byte_x2) {
            outb(0x3CF, mask_start & mask_end);
            volatile uint8_t* ptr = row_ptr + byte_x1;
            volatile uint8_t dummy = *ptr;
            *ptr = 0xFF;
            (void) dummy;
        } else {
            outb(0x3CF, mask_start);
            volatile uint8_t* ptr = row_ptr + byte_x1;
            volatile uint8_t dummy = *ptr;
            *ptr = 0xFF;

            if (byte_x2 - byte_x1 > 1) {
                outb(0x3CF, 0xFF);
                for (int x = byte_x1 + 1; x < byte_x2; x++) {
                    ptr = row_ptr + x;
                    dummy = *ptr;
                    *ptr = 0xFF;
                }
            }

            outb(0x3CF, mask_end);
            ptr = row_ptr + byte_x2;
            dummy = *ptr;
            *ptr = 0xFF;

            (void) dummy;
        }
    }

    outb(0x3CE, 0x03); outb(0x3CF, 0x00);
    outb(0x3CE, 0x08); outb(0x3CF, 0xFF);
}