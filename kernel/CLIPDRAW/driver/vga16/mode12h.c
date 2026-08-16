#include "vga16.h"


static const uint8_t g_mode12h_seq[]  = { 
    0x03, 0x01, 0x0F, 0x00, 0x06
};

static const uint8_t g_mode12h_crtc[] = { 
    0x5F, 0x4F, 0x50, 0x82, 0x54, 0x80, 0x0B, 0x3E, 
    0x00, 0x40, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
    0xEA, 0x8C, 0xDF, 0x28, 0x00, 0xE7, 0x04, 0xE3, 0xFF 
};

static const uint8_t g_mode12h_gc[]   = { 
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x05, 0x0F, 0xFF 
};

static const uint8_t g_mode12h_attr[] = { 
    0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x14, 0x07, 
    0x38, 0x39, 0x3A, 0x3B, 0x3C, 0x3D, 0x3E, 0x3F, 
    0x01, 0x00, 0x0F, 0x00, 0x00 
};

void SwitchToMode12h(void) {
    outb(VGA_MISC_WRITE, 0xE3);
    for (uint8_t i = 0; i < 5; i++) {
        outb(VGA_SEQ_INDEX, i);
        outb(VGA_SEQ_DATA, g_mode12h_seq[i]);
    }
    outb(VGA_CRTC_INDEX, 0x11);
    outb(VGA_CRTC_DATA, inb(VGA_CRTC_DATA) & ~0x80);

    for (uint8_t i = 0; i < 25; i++) {
        outb(VGA_CRTC_INDEX, i);
        outb(VGA_CRTC_DATA, g_mode12h_crtc[i]);
    }

    for (uint8_t i = 0; i < 9; i++) {
        outb(VGA_GC_INDEX, i);
        outb(VGA_GC_DATA, g_mode12h_gc[i]);
    }

    for (uint8_t i = 0; i < 21; i++) {
        (void)inb(VGA_INSTAT_READ);
        outb(VGA_ATTR_INDEX, i);
        outb(VGA_ATTR_DATA, g_mode12h_attr[i]);
    }

    (void)inb(VGA_INSTAT_READ);
    outb(VGA_ATTR_INDEX, 0x20);

    (void)inb(0x3DA);
    outb(0x3C0, 0x20);
    outb(VGA_SEQ_INDEX, 0x02);
    outb(VGA_SEQ_DATA, 0x0F);
    outb(VGA_GC_INDEX, 0x08);
    outb(VGA_GC_DATA, 0xFF);
    outb(VGA_GC_INDEX, 0x03);
    outb(VGA_GC_DATA, 0x00);
    outb(VGA_GC_INDEX, 0x05);
    outb(VGA_GC_DATA, 0x00);
}
