#include "vga16.h"

void VgaSimpleRect(int x1, int y1, int x2, int y2, uint8_t color) {
    volatile uint8_t* vram = VRAM_BASE;
    volatile uint8_t dummy;

    int start_byte = x1 >> 3;
    int end_byte   = (x2 - 1) >> 3;

    uint8_t left_mask  = 0xFF >> (x1 & 7);
    uint8_t right_mask = 0xFF << (7 - ((x2 - 1) & 7));

    // 1. Set VGA Graphics Controller to Write Mode 2 (bits 0-1 = 2)
    outb(VGA_GC_INDEX, 0x05); 
    outb(VGA_GC_DATA, 0x02);

    color &= 0x0F; // Ensure valid 4-bit palette index

    // Single-byte horizontal width case
    if (start_byte == end_byte) {
        uint8_t edge_mask = left_mask & right_mask;
        outb(VGA_GC_INDEX, 0x08); 
        outb(VGA_GC_DATA, edge_mask);

        for (int y = y1; y < y2; y++) {
            volatile uint8_t* ptr = vram + (y * BYTES_PER_ROW) + start_byte;
            dummy = *ptr;  // Latch background colors
            *ptr = color;  // Write Mode 2 fills color to masked bits in 1 write
        }
    } 
    // Multi-byte horizontal width case
    else {
        // 1. Left Edge Column (Single outb for all rows)
        outb(VGA_GC_INDEX, 0x08); 
        outb(VGA_GC_DATA, left_mask);
        for (int y = y1; y < y2; y++) {
            volatile uint8_t* ptr = vram + (y * BYTES_PER_ROW) + start_byte;
            dummy = *ptr;
            *ptr = color;
        }

        // 2. Middle Aligned Bytes (Single outb for all rows)
        if (start_byte + 1 < end_byte) {
            outb(VGA_GC_INDEX, 0x08); 
            outb(VGA_GC_DATA, 0xFF);
            for (int y = y1; y < y2; y++) {
                volatile uint8_t* ptr = vram + (y * BYTES_PER_ROW) + start_byte + 1;
                for (int b = start_byte + 1; b < end_byte; b++) {
                    dummy = *ptr;
                    *ptr++ = color;
                }
            }
        }

        // 3. Right Edge Column (Single outb for all rows)
        outb(VGA_GC_INDEX, 0x08); 
        outb(VGA_GC_DATA, right_mask);
        for (int y = y1; y < y2; y++) {
            volatile uint8_t* ptr = vram + (y * BYTES_PER_ROW) + end_byte;
            dummy = *ptr;
            *ptr = color;
        }
    }

    // Restore standard bitmask register state
    outb(VGA_GC_INDEX, 0x08); 
    outb(VGA_GC_DATA, 0xFF);
    (void)dummy;
}

void VgaDrawRectBayer4x4(int x1, int y1, int x2, int y2, uint8_t color1, uint8_t color2, int level) { 
    if (color1 == color2 || level == 0) {
        VgaSimpleRect(x1, y1, x2, y2, color1);
        return;
    }   
    if (level == 16) {
        VgaSimpleRect(x1, y1, x2, y2, color2);
        return;
    }
    volatile uint8_t* vram = VRAM_BASE;
    volatile uint8_t dummy;

    outb(VGA_GC_INDEX, 0x05); outb(VGA_GC_DATA, 0x00);
    outb(VGA_GC_INDEX, 0x01); outb(VGA_GC_DATA, 0x0F);

    for (int y = y1; y < y2; y++) {
        int start_byte = x1 >> 3;
        int end_byte   = (x2 - 1) >> 3;              // FIX: was `x2 >> 3`
        uint32_t row_offset = y * 80;

        uint8_t pattern2 = bayer_4x4_masks[level][y & 3];
        uint8_t pattern1 = ~pattern2;

        uint8_t left_mask  = 0xFF >> (x1 & 7);
        uint8_t right_mask = 0xFF << (7 - ((x2 - 1) & 7));  // FIX: was `~(0xFF >> ((x2 & 7) + 1))`

        if (start_byte == end_byte) {
            uint8_t edge_mask = left_mask & right_mask;
            volatile uint8_t* ptr = vram + row_offset + start_byte;

            outb(VGA_GC_INDEX, 0x00); outb(VGA_GC_DATA, color1 & 0x0F);
            outb(VGA_GC_INDEX, 0x08); outb(VGA_GC_DATA, edge_mask & pattern1);
            dummy = *ptr; *ptr = 0xFF;

            outb(VGA_GC_INDEX, 0x00); outb(VGA_GC_DATA, color2 & 0x0F);
            outb(VGA_GC_INDEX, 0x08); outb(VGA_GC_DATA, edge_mask & pattern2);
            dummy = *ptr; *ptr = 0xFF;
        }
        else {
            volatile uint8_t* ptr = vram + row_offset + start_byte;

            outb(VGA_GC_INDEX, 0x00); outb(VGA_GC_DATA, color1 & 0x0F);
            outb(VGA_GC_INDEX, 0x08); outb(VGA_GC_DATA, left_mask & pattern1);
            dummy = *ptr; *ptr = 0xFF;

            outb(VGA_GC_INDEX, 0x00); outb(VGA_GC_DATA, color2 & 0x0F);
            outb(VGA_GC_INDEX, 0x08); outb(VGA_GC_DATA, left_mask & pattern2);
            dummy = *ptr; *ptr++ = 0xFF;

            outb(VGA_GC_INDEX, 0x00); outb(VGA_GC_DATA, color1 & 0x0F);
            outb(VGA_GC_INDEX, 0x08); outb(VGA_GC_DATA, pattern1);
            for (int b = start_byte + 1; b < end_byte; b++) {
                dummy = *ptr;
                *ptr++ = 0xFF;
            }

            ptr = vram + row_offset + start_byte + 1;

            outb(VGA_GC_INDEX, 0x00); outb(VGA_GC_DATA, color2 & 0x0F);
            outb(VGA_GC_INDEX, 0x08); outb(VGA_GC_DATA, pattern2);
            for (int b = start_byte + 1; b < end_byte; b++) {
                dummy = *ptr;
                *ptr++ = 0xFF;
            }

            outb(VGA_GC_INDEX, 0x00); outb(VGA_GC_DATA, color1 & 0x0F);
            outb(VGA_GC_INDEX, 0x08); outb(VGA_GC_DATA, right_mask & pattern1);
            dummy = *ptr; *ptr = 0xFF;

            outb(VGA_GC_INDEX, 0x00); outb(VGA_GC_DATA, color2 & 0x0F);
            outb(VGA_GC_INDEX, 0x08); outb(VGA_GC_DATA, right_mask & pattern2);
            dummy = *ptr; *ptr = 0xFF;
        }
    }

    outb(VGA_GC_INDEX, 0x00); outb(VGA_GC_DATA, 0x00);
    outb(VGA_GC_INDEX, 0x01); outb(VGA_GC_DATA, 0x00);
    outb(VGA_GC_INDEX, 0x08); outb(VGA_GC_DATA, 0xFF);
    (void)dummy;
}

// Paints a rectangle using a single dithered brush colour (colorX1/colorX2
// via bayer at `level`), restricted to the half of `pattern` selected by
// use_secondary_slot. Pixels outside that pattern half are never written
// to (the GC bitmask simply never includes those bits), so this is safe
// to use for "one brush colour opaque, one transparent" without doing
// any real alpha blending.
void VgaSoloBrushRect(int x1, int y1, int x2, int y2,
                       uint8_t color1, uint8_t color2, int level,
                       uint8_t* pattern, int use_secondary_slot) {
    volatile uint8_t* vram = VRAM_BASE;
    volatile uint8_t dummy;

    outb(VGA_GC_INDEX, 0x05); outb(VGA_GC_DATA, 0x00);
    outb(VGA_GC_INDEX, 0x01); outb(VGA_GC_DATA, 0x0F);

    for (int y = y1; y < y2; y++) {
        int start_byte = x1 >> 3;
        int end_byte   = (x2 - 1) >> 3;
        uint32_t row_offset = y * 80;

        uint8_t bayer2 = bayer_4x4_masks[level][y & 3];
        uint8_t bayer1 = ~bayer2;

        uint8_t sel = pattern[y & 7];
        if (use_secondary_slot) sel = (uint8_t)~sel;

        uint8_t left_mask  = 0xFF >> (x1 & 7);
        uint8_t right_mask = 0xFF << (7 - ((x2 - 1) & 7));

        if (start_byte == end_byte) {
            uint8_t edge_mask = left_mask & right_mask;
            volatile uint8_t* ptr = vram + row_offset + start_byte;

            outb(VGA_GC_INDEX, 0x00); outb(VGA_GC_DATA, color1 & 0x0F);
            outb(VGA_GC_INDEX, 0x08); outb(VGA_GC_DATA, edge_mask & sel & bayer1);
            dummy = *ptr; *ptr = 0xFF;

            outb(VGA_GC_INDEX, 0x00); outb(VGA_GC_DATA, color2 & 0x0F);
            outb(VGA_GC_INDEX, 0x08); outb(VGA_GC_DATA, edge_mask & sel & bayer2);
            dummy = *ptr; *ptr = 0xFF;
        }
        else {
            volatile uint8_t* ptr;

            // --- LEFT EDGE ---
            ptr = vram + row_offset + start_byte;
            outb(VGA_GC_INDEX, 0x00); outb(VGA_GC_DATA, color1 & 0x0F);
            outb(VGA_GC_INDEX, 0x08); outb(VGA_GC_DATA, left_mask & sel & bayer1);
            dummy = *ptr; *ptr = 0xFF;

            outb(VGA_GC_INDEX, 0x00); outb(VGA_GC_DATA, color2 & 0x0F);
            outb(VGA_GC_INDEX, 0x08); outb(VGA_GC_DATA, left_mask & sel & bayer2);
            dummy = *ptr; *ptr = 0xFF;

            // --- MIDDLE FULL BYTES ---
            outb(VGA_GC_INDEX, 0x00); outb(VGA_GC_DATA, color1 & 0x0F);
            outb(VGA_GC_INDEX, 0x08); outb(VGA_GC_DATA, sel & bayer1);
            ptr = vram + row_offset + start_byte + 1;
            for (int b = start_byte + 1; b < end_byte; b++) { dummy = *ptr; *ptr++ = 0xFF; }

            outb(VGA_GC_INDEX, 0x00); outb(VGA_GC_DATA, color2 & 0x0F);
            outb(VGA_GC_INDEX, 0x08); outb(VGA_GC_DATA, sel & bayer2);
            ptr = vram + row_offset + start_byte + 1;
            for (int b = start_byte + 1; b < end_byte; b++) { dummy = *ptr; *ptr++ = 0xFF; }

            // --- RIGHT EDGE ---
            ptr = vram + row_offset + end_byte;
            outb(VGA_GC_INDEX, 0x00); outb(VGA_GC_DATA, color1 & 0x0F);
            outb(VGA_GC_INDEX, 0x08); outb(VGA_GC_DATA, right_mask & sel & bayer1);
            dummy = *ptr; *ptr = 0xFF;

            outb(VGA_GC_INDEX, 0x00); outb(VGA_GC_DATA, color2 & 0x0F);
            outb(VGA_GC_INDEX, 0x08); outb(VGA_GC_DATA, right_mask & sel & bayer2);
            dummy = *ptr; *ptr = 0xFF;
        }
    }

    outb(VGA_GC_INDEX, 0x00); outb(VGA_GC_DATA, 0x00);
    outb(VGA_GC_INDEX, 0x01); outb(VGA_GC_DATA, 0x00);
    outb(VGA_GC_INDEX, 0x08); outb(VGA_GC_DATA, 0xFF);
    (void)dummy;
}

void VgaDiabolicalRect(int x1, int y1, int x2, int y2,
                           uint8_t primary_color1, uint8_t primary_color2, int primary_level,
                           uint8_t secondary_color1, uint8_t secondary_color2, int secondary_level,
                           uint8_t* pattern) {
    volatile uint8_t* vram = VRAM_BASE;
    volatile uint8_t dummy;

    outb(VGA_GC_INDEX, 0x05); outb(VGA_GC_DATA, 0x00);
    outb(VGA_GC_INDEX, 0x01); outb(VGA_GC_DATA, 0x0F);

    for (int y = y1; y < y2; y++) {
        int start_byte = x1 >> 3;
        int end_byte   = (x2 - 1) >> 3;
        uint32_t row_offset = y * 80;

        // Bayer dither split for each of the two fills, this row
        uint8_t primary_pattern2   = bayer_4x4_masks[primary_level][y & 3];
        uint8_t primary_pattern1   = ~primary_pattern2;
        uint8_t secondary_pattern2 = bayer_4x4_masks[secondary_level][y & 3];
        uint8_t secondary_pattern1 = ~secondary_pattern2;

        // Brush selector for this row. Every VRAM byte covers exactly 8
        // consecutive x pixels starting on a multiple of 8, so pattern[y%8]
        // lines up bit-for-bit with EVERY byte in the row (left/middle/
        // right) with no shifting needed -- same trick the Bayer masks
        // already relied on above.
        uint8_t primary_sel   = pattern[y & 7];
        uint8_t secondary_sel = (uint8_t)~primary_sel;

        uint8_t left_mask  = 0xFF >> (x1 & 7);
        uint8_t right_mask = 0xFF << (7 - ((x2 - 1) & 7));

        if (start_byte == end_byte) {
            uint8_t edge_mask = left_mask & right_mask;
            volatile uint8_t* ptr = vram + row_offset + start_byte;

            outb(VGA_GC_INDEX, 0x00); outb(VGA_GC_DATA, primary_color1 & 0x0F);
            outb(VGA_GC_INDEX, 0x08); outb(VGA_GC_DATA, edge_mask & primary_sel & primary_pattern1);
            dummy = *ptr; *ptr = 0xFF;

            outb(VGA_GC_INDEX, 0x00); outb(VGA_GC_DATA, primary_color2 & 0x0F);
            outb(VGA_GC_INDEX, 0x08); outb(VGA_GC_DATA, edge_mask & primary_sel & primary_pattern2);
            dummy = *ptr; *ptr = 0xFF;

            outb(VGA_GC_INDEX, 0x00); outb(VGA_GC_DATA, secondary_color1 & 0x0F);
            outb(VGA_GC_INDEX, 0x08); outb(VGA_GC_DATA, edge_mask & secondary_sel & secondary_pattern1);
            dummy = *ptr; *ptr = 0xFF;

            outb(VGA_GC_INDEX, 0x00); outb(VGA_GC_DATA, secondary_color2 & 0x0F);
            outb(VGA_GC_INDEX, 0x08); outb(VGA_GC_DATA, edge_mask & secondary_sel & secondary_pattern2);
            dummy = *ptr; *ptr = 0xFF;
        }
        else {
            volatile uint8_t* ptr;

            // --- LEFT EDGE ---
            ptr = vram + row_offset + start_byte;

            outb(VGA_GC_INDEX, 0x00); outb(VGA_GC_DATA, primary_color1 & 0x0F);
            outb(VGA_GC_INDEX, 0x08); outb(VGA_GC_DATA, left_mask & primary_sel & primary_pattern1);
            dummy = *ptr; *ptr = 0xFF;

            outb(VGA_GC_INDEX, 0x00); outb(VGA_GC_DATA, primary_color2 & 0x0F);
            outb(VGA_GC_INDEX, 0x08); outb(VGA_GC_DATA, left_mask & primary_sel & primary_pattern2);
            dummy = *ptr; *ptr = 0xFF;

            outb(VGA_GC_INDEX, 0x00); outb(VGA_GC_DATA, secondary_color1 & 0x0F);
            outb(VGA_GC_INDEX, 0x08); outb(VGA_GC_DATA, left_mask & secondary_sel & secondary_pattern1);
            dummy = *ptr; *ptr = 0xFF;

            outb(VGA_GC_INDEX, 0x00); outb(VGA_GC_DATA, secondary_color2 & 0x0F);
            outb(VGA_GC_INDEX, 0x08); outb(VGA_GC_DATA, left_mask & secondary_sel & secondary_pattern2);
            dummy = *ptr; *ptr = 0xFF;

            // --- MIDDLE FULL BYTES (one full sweep per pass, 4 passes) ---
            outb(VGA_GC_INDEX, 0x00); outb(VGA_GC_DATA, primary_color1 & 0x0F);
            outb(VGA_GC_INDEX, 0x08); outb(VGA_GC_DATA, primary_sel & primary_pattern1);
            ptr = vram + row_offset + start_byte + 1;
            for (int b = start_byte + 1; b < end_byte; b++) { dummy = *ptr; *ptr++ = 0xFF; }

            outb(VGA_GC_INDEX, 0x00); outb(VGA_GC_DATA, primary_color2 & 0x0F);
            outb(VGA_GC_INDEX, 0x08); outb(VGA_GC_DATA, primary_sel & primary_pattern2);
            ptr = vram + row_offset + start_byte + 1;
            for (int b = start_byte + 1; b < end_byte; b++) { dummy = *ptr; *ptr++ = 0xFF; }

            outb(VGA_GC_INDEX, 0x00); outb(VGA_GC_DATA, secondary_color1 & 0x0F);
            outb(VGA_GC_INDEX, 0x08); outb(VGA_GC_DATA, secondary_sel & secondary_pattern1);
            ptr = vram + row_offset + start_byte + 1;
            for (int b = start_byte + 1; b < end_byte; b++) { dummy = *ptr; *ptr++ = 0xFF; }

            outb(VGA_GC_INDEX, 0x00); outb(VGA_GC_DATA, secondary_color2 & 0x0F);
            outb(VGA_GC_INDEX, 0x08); outb(VGA_GC_DATA, secondary_sel & secondary_pattern2);
            ptr = vram + row_offset + start_byte + 1;
            for (int b = start_byte + 1; b < end_byte; b++) { dummy = *ptr; *ptr++ = 0xFF; }

            // --- RIGHT EDGE ---
            ptr = vram + row_offset + end_byte;

            outb(VGA_GC_INDEX, 0x00); outb(VGA_GC_DATA, primary_color1 & 0x0F);
            outb(VGA_GC_INDEX, 0x08); outb(VGA_GC_DATA, right_mask & primary_sel & primary_pattern1);
            dummy = *ptr; *ptr = 0xFF;

            outb(VGA_GC_INDEX, 0x00); outb(VGA_GC_DATA, primary_color2 & 0x0F);
            outb(VGA_GC_INDEX, 0x08); outb(VGA_GC_DATA, right_mask & primary_sel & primary_pattern2);
            dummy = *ptr; *ptr = 0xFF;

            outb(VGA_GC_INDEX, 0x00); outb(VGA_GC_DATA, secondary_color1 & 0x0F);
            outb(VGA_GC_INDEX, 0x08); outb(VGA_GC_DATA, right_mask & secondary_sel & secondary_pattern1);
            dummy = *ptr; *ptr = 0xFF;

            outb(VGA_GC_INDEX, 0x00); outb(VGA_GC_DATA, secondary_color2 & 0x0F);
            outb(VGA_GC_INDEX, 0x08); outb(VGA_GC_DATA, right_mask & secondary_sel & secondary_pattern2);
            dummy = *ptr; *ptr = 0xFF;
        }
    }

    outb(VGA_GC_INDEX, 0x00); outb(VGA_GC_DATA, 0x00);
    outb(VGA_GC_INDEX, 0x01); outb(VGA_GC_DATA, 0x00);
    outb(VGA_GC_INDEX, 0x08); outb(VGA_GC_DATA, 0xFF);
    (void)dummy;
}


void VgaPutSolidRect(struct graphics_driver*, int x1, int y1, int x2, int y2, uint32_t colour) {
    if (!IsOpaqueColour(colour)) {
        return;
    }
    if (x1 >= 640 || y1 >= 480) return;
    if (x1 >= x2) { return; }
    if (y1 >= y2) { return; }
    if (x1 < 0) x1 = 0;
    if (x2 > 640) x2 = 640;
    if (y1 < 0) y1 = 0;
    if (y2 > 480) y2 = 480;

    uint8_t colours = 0;
    uint8_t level = 0;
    FindVgaColours(colour, &colours, &level);
    VgaDrawRectBayer4x4(x1, y1, x2, y2, colours >> 4, colours & 0xF, level);
}


void VgaPutBrushRect(struct graphics_driver*, int x1, int y1, int x2, int y2, uint32_t primary,
    uint32_t secondary, uint8_t* pattern) {

    if (x1 >= 640 || y1 >= 480) return;
    if (x1 >= x2) { return; }
    if (y1 >= y2) { return; }
    if (x1 < 0) x1 = 0;
    if (x2 > 640) x2 = 640;
    if (y1 < 0) y1 = 0;
    if (y2 > 480) y2 = 480;

    int primary_opaque   = IsOpaqueColour(primary);
    int secondary_opaque = IsOpaqueColour(secondary);

    if (!primary_opaque && !secondary_opaque) {
        return;
    }

    if (primary_opaque && secondary_opaque) {
        uint8_t pcolours = 0, plevel = 0, scolours = 0, slevel = 0;
        FindVgaColours(primary, &pcolours, &plevel);
        FindVgaColours(secondary, &scolours, &slevel);

        VgaDiabolicalRect(
            x1, y1, x2, y2,
            pcolours >> 4, pcolours & 0xF, plevel,
            scolours >> 4, scolours & 0xF, slevel,
            pattern
        );
        return;
    }

    if (primary_opaque) {
        uint8_t pcolours = 0, plevel = 0;
        FindVgaColours(primary, &pcolours, &plevel);
        VgaSoloBrushRect(x1, y1, x2, y2, pcolours >> 4, pcolours & 0xF, plevel,
                          pattern, 0);
    } else {
        uint8_t scolours = 0, slevel = 0;
        FindVgaColours(secondary, &scolours, &slevel);
        VgaSoloBrushRect(x1, y1, x2, y2, scolours >> 4, scolours & 0xF, slevel,
                          pattern, 1);
    }
}