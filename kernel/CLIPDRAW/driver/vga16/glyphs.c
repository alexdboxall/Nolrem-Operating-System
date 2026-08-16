#include "vga16.h"

static uint8_t vga_glyphs[95][14];

uint8_t* GetVgaGlyph(char c) {
    return vga_glyphs[CLAMP(c - 32, 0, 94)];
}

void ReadVgaGlyphs(void) {
    outb(VGA_SEQ_INDEX, 0x02); uint8_t orig_seq02 = inb(VGA_SEQ_DATA);
    outb(VGA_SEQ_INDEX, 0x04); uint8_t orig_seq04 = inb(VGA_SEQ_DATA);
    outb(VGA_GC_INDEX,  0x04); uint8_t orig_gc04  = inb(VGA_GC_DATA);
    outb(VGA_GC_INDEX,  0x05); uint8_t orig_gc05  = inb(VGA_GC_DATA);
    outb(VGA_GC_INDEX,  0x06); uint8_t orig_gc06  = inb(VGA_GC_DATA);
    outb(VGA_SEQ_INDEX, 0x02); outb(VGA_SEQ_DATA, 0x04);
    outb(VGA_SEQ_INDEX, 0x04); outb(VGA_SEQ_DATA, 0x07);
    outb(VGA_GC_INDEX, 0x04); outb(VGA_GC_DATA, 0x02);
    outb(VGA_GC_INDEX, 0x05); outb(VGA_GC_DATA, 0x00);
    outb(VGA_GC_INDEX, 0x06); outb(VGA_GC_DATA, 0x00);

    volatile uint8_t* font_vram = VRAM_BASE;

    for (int c = 32; c < 127; ++c) {
        for (uint8_t row = 1; row < 15; row++) {
            vga_glyphs[c - 32][row - 1] = font_vram[(c * 32) + row];
        }
    }
    
    outb(VGA_GC_INDEX, 0x04); outb(VGA_GC_DATA, orig_gc04);
    outb(VGA_GC_INDEX, 0x05); outb(VGA_GC_DATA, orig_gc05);
    outb(VGA_GC_INDEX, 0x06); outb(VGA_GC_DATA, orig_gc06);
    outb(VGA_SEQ_INDEX, 0x02); outb(VGA_SEQ_DATA, orig_seq02);
    outb(VGA_SEQ_INDEX, 0x04); outb(VGA_SEQ_DATA, orig_seq04);
}
