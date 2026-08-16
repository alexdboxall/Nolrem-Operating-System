#include "vga16.h"

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

colour_t ConvertVgaColourToARGB(int vgacol) {
    RGB16 color = vga_palette[vgacol];
    return 0xFF000000 | (color.r << 16) | (color.g << 8) | color.b;
}

void FindVgaColours(uint32_t rgb, uint8_t* col_out, uint8_t* level_out) {
    /* Optimise some common colours. */
    if (rgb == 0x000000) {
        *col_out = 0;
        return;
    }
    if (rgb == 0xFFFFFF) {
        *col_out = 0xFF;
        return;
    }
    if (rgb == 0x808080) {
        *col_out = 0x88;
        return;
    }
    if (rgb == 0xC0C0C0) {
        *col_out = 0x77;
        return;
    }

    int16_t tr = (int16_t)((rgb >> 16) & 0xFF);
    int16_t tg = (int16_t)((rgb >> 8)  & 0xFF);
    int16_t tb = (int16_t)( rgb        & 0xFF);

    int32_t solid_err = 0x7FFFFFFF;
    uint8_t solid_i = 0;
    for (uint8_t i = 0; i < 16; i++) {
        int32_t er = tr - vga_palette[i].r;
        int32_t eg = tg - vga_palette[i].g;
        int32_t eb = tb - vga_palette[i].b;
        int32_t err = WR*er*er + WG*eg*eg + WB*eb*eb;
        if (err < solid_err) { solid_err = err; solid_i = i; }
    }

    int32_t dither_err = 0x7FFFFFFF;
    uint8_t dither_i = 0, dither_j = 0, dither_level = 0;

    const int MAX_LEVELS = 16;
    const int MAX_LEVELS_LOG2 = 4;

    for (uint8_t i = 0; i < 16; i++) {
        int16_t pir = vga_palette[i].r;
        int16_t pig = vga_palette[i].g;
        int16_t pib = vga_palette[i].b;

        for (uint8_t j = i + 1; j < 16; j++) {
            int16_t dx = vga_palette[j].r - pir;
            int16_t dy = vga_palette[j].g - pig;
            int16_t dz = vga_palette[j].b - pib;

            int16_t tx = tr - pir;
            int16_t ty = tg - pig;
            int16_t tz = tb - pib;

            int32_t A = (int32_t)WR*dx*dx + (int32_t)WG*dy*dy + (int32_t)WB*dz*dz;
            int32_t B = (int32_t)WR*tx*dx + (int32_t)WG*ty*dy + (int32_t)WB*tz*dz;

            int32_t level32;
            if (B <= 0)      level32 = 0;
            else if (B >= A) level32 = MAX_LEVELS;
            else {
                level32 = (MAX_LEVELS*B + (A >> 1)) / A;
                if (level32 > MAX_LEVELS) level32 = MAX_LEVELS;
            }

            // Endpoints aren't dithers -- pass 1 already covers them.
            if (level32 == 0 || level32 == MAX_LEVELS) continue;

            int32_t cr = pir + ((level32*(int32_t)dx) >> MAX_LEVELS_LOG2);
            int32_t cg = pig + ((level32*(int32_t)dy) >> MAX_LEVELS_LOG2);
            int32_t cb = pib + ((level32*(int32_t)dz) >> MAX_LEVELS_LOG2);

            int32_t er = tr - cr;
            int32_t eg = tg - cg;
            int32_t eb = tb - cb;
            int32_t err = WR*er*er + WG*eg*eg + WB*eb*eb;

            if (err < dither_err) {
                dither_err   = err;
                dither_i     = i;
                dither_j     = j;
                dither_level = (uint8_t)level32;
            }
        }
    }

    // Only accept the dither if it earns its keep.
    if (dither_err + DITHER_BIAS < solid_err) {
        *col_out   = (dither_i << 4) | dither_j;
        *level_out = dither_level;
    } else {
        *col_out   = (solid_i << 4) | solid_i;
        *level_out = 0;
    }
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