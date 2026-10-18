#include "vga16.h"
#include <stdatomic.h>

#define WR 3
#define WG 4
#define WB 2

#define DITHER_BIAS 1000

typedef struct { uint8_t r, g, b; } RGB16;
static const RGB16 vga_palette[16] = {
    {  0,  0,  0}, // 0  black
    {128,  0,  0}, // 1  maroon (dark red)
    {  0,128,  0}, // 2  green (dark green)
    {128,128,  0}, // 3  olive (dark yellow -- no "brown" in this palette)
    {  0,  0,128}, // 4  navy (dark blue)
    {128,  0,128}, // 5  purple (dark magenta)
    {  0,128,128}, // 6  teal (dark cyan)
    {192,192,192}, // 7  silver (light gray)
    {128,128,128}, // 8  gray (dark gray)
    {255,  0,  0}, // 9  red
    {  0,255,  0}, // 10 lime (green)
    {255,255,  0}, // 11 yellow
    {  0,  0,255}, // 12 blue
    {255,  0,255}, // 13 fuchsia (magenta)
    {  0,255,255}, // 14 aqua (cyan)
    {255,255,255}, // 15 white
};

uint8_t VgaNearestColour(uint32_t col) {
    /* Fast case for black text. */
    if (col == 0xFF000000U) {
        return 0;
    }
    if ((col >> 24) != 0xFF) {
        /* If transparent at all, we don't care about colour. */
        return 0;
    }
 
    /* Check if the same as the previous colour. */
    static _Atomic uint32_t cached = 0;
    uint32_t cached_r = atomic_load_explicit(&cached, memory_order_relaxed);
    if ((cached_r & 0xFFFFFF) == (col & 0xFFFFFF)) {
        return cached_r >> 24;
    }
 
    int r = (int)((col >> 16) & 0xFF);
    int g = (int)((col >> 8)  & 0xFF);
    int b = (int)( col        & 0xFF);
 
    int best      = 0;
    int best_dist = 0x7FFFFFFF;
    for (int i = 0; i < 16; i++) {
        int dr = r - vga_palette[i].r;
        int dg = g - vga_palette[i].g;
        int db = b - vga_palette[i].b;
        int d  = dr * dr + dg * dg + db * db;
        if (d < best_dist) {
            best_dist = d;
            best = i;
            if (d == 0) break; // exact palette hit, can't do better
        }
    }
 
    atomic_store_explicit(&cached, (((uint32_t) best) << 24) | (col & 0xFFFFFF), memory_order_relaxed);
    return best;
}

colour_t ConvertVgaColourToARGB(int vgacol) {
    RGB16 color = vga_palette[vgacol];
    return 0xFF000000 | (color.r << 16) | (color.g << 8) | color.b;
}

static inline void WaitVRetraceReset(void) {
    inb(VGA_INSTAT_READ);
}

void InitVgaPalette(void) {
    WaitVRetraceReset();
    for (uint8_t i = 0; i < 16; i++) {
        outb(VGA_ATTR_INDEX, i);
        outb(VGA_ATTR_DATA,  i);
    }

    WaitVRetraceReset();
    outb(VGA_ATTR_INDEX, 0x20);

    outb(VGA_DAC_WRITE_INDEX, 0);
    for (uint8_t i = 0; i < 16; i++) {
        outb(VGA_DAC_DATA, vga_palette[i].r >> 2);
        outb(VGA_DAC_DATA, vga_palette[i].g >> 2);
        outb(VGA_DAC_DATA, vga_palette[i].b >> 2);
    }
}