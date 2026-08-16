#include "vga16.h"

// bayer_4x4_masks[level][y & 3]

void VgaDiabolicalLine(int x1, int y1, int x2, int y2,
                           uint8_t color1, uint8_t color2, int level,
                           uint8_t* pattern, int thickness, int pat_width, int pat_height) {
    volatile uint8_t* vram = VRAM_BASE;
    volatile uint8_t dummy;

    outb(VGA_GC_INDEX, 0x05); outb(VGA_GC_DATA, 0x00);
    outb(VGA_GC_INDEX, 0x01); outb(VGA_GC_DATA, 0x0F);

    // ???
    (void) x1;
    (void) x2;
    (void) y1;
    (void) y2;
    (void) color1;
    (void) color2;
    (void) level;
    (void) pattern;
    (void) thickness;
    (void) pat_height;
    (void) pat_width;
    (void) vram;

    outb(VGA_GC_INDEX, 0x00); outb(VGA_GC_DATA, 0x00);
    outb(VGA_GC_INDEX, 0x01); outb(VGA_GC_DATA, 0x00);
    outb(VGA_GC_INDEX, 0x08); outb(VGA_GC_DATA, 0xFF);
    (void)dummy;
}

export void VgaPenLine(struct graphics_driver*, int x1, int y1, int x2, int y2, uint32_t colour, int thickness, uint8_t* pattern, int pat_width, int pat_height) {
    if (x1 >= 640 || y1 >= 480) return;
    if (x1 >= x2) { return; }
    if (y1 >= y2) { return; }
    if (x1 < 0) x1 = 0;
    if (x2 > 640) x2 = 640;
    if (y1 < 0) y1 = 0;
    if (y2 > 480) y2 = 480;

    if (!IsOpaqueColour(colour)) {
        return;
    }

    uint8_t pcolours = 0, plevel = 0;
    FindVgaColours(colour, &pcolours, &plevel);

    VgaDiabolicalLine(
        x1, y1, x2, y2,
        pcolours >> 4, pcolours & 0xF, plevel,
        pattern, thickness, pat_width, pat_height
    );
}