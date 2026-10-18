#include "vga16.h"

void VGADrawGlyph(struct graphics_driver*, int x, int y, struct rect bound, uint32_t col,
    int glyph_w_px, int glyph_h, uint8_t* glyph_col_maj_order) {
    volatile uint8_t* vram = VRAM_BASE;
    volatile uint8_t dummy;
 
    if ((col >> 24) != 0xFF) return;
    if (glyph_w_px <= 0 || glyph_h <= 0 || !glyph_col_maj_order) return;
 
    // Clip the glyph's box against the bound rect, then the screen.
    int x1 = x,               y1 = y;
    int x2 = x + glyph_w_px,  y2 = y + glyph_h;
    int bx2 = bound.x + bound.w;
    int by2 = bound.y + bound.h;
    if (x1 < bound.x) x1 = bound.x;
    if (y1 < bound.y) y1 = bound.y;
    if (x2 > bx2) x2 = bx2;
    if (y2 > by2) y2 = by2;
    if (x1 < 0) x1 = 0;
    if (y1 < 0) y1 = 0;
    if (x2 > 640) x2 = 640;
    if (y2 > 480) y2 = 480;
    if (x1 >= x2 || y1 >= y2) return;
 
    uint8_t colour = VgaNearestColour(col) & 0x0F;
 
    int strips     = (glyph_w_px + 7) >> 3;
    int base_byte  = x >> 3;  // VRAM byte holding glyph pixel 0 (arithmetic shift, so floor for x < 0)
    int shift      = x & 7;   // how far the glyph is offset into that byte
 
    // Visible VRAM byte range and the edge masks for the clipped box.
    int start_byte = x1 >> 3;
    int end_byte   = (x2 - 1) >> 3;
    uint8_t left_mask  = 0xFF >> (x1 & 7);
    uint8_t right_mask = 0xFF << (7 - ((x2 - 1) & 7));
 
    // Glyph strips worth visiting. Strip k lands in VRAM bytes base_byte+k
    // (high part) and base_byte+k+1 (spill-over), so start one strip early
    // to pick up the spill-over into start_byte. k == strips is the final
    // spill-over-only byte.
    int k_lo = start_byte - base_byte - 1;
    if (k_lo < 0) k_lo = 0;
    int k_hi = end_byte - base_byte;
    if (k_hi > strips) k_hi = strips;
 
    // Write Mode 2, then leave the GC index on the Bit Mask register so the
    // loop below only has to write the data port.
    outb(VGA_GC_INDEX, 0x05);
    outb(VGA_GC_DATA, 0x02);
    outb(VGA_GC_INDEX, 0x08);
    int current_mask = -1;
 
    for (int sy = y1; sy < y2; sy++) {
        const uint8_t* src = glyph_col_maj_order + (sy - y);
        volatile uint8_t* row = vram + (uint32_t)sy * BYTES_PER_ROW;
        uint16_t carry = 0; // bits spilled into the next VRAM byte by the previous strip
 
        for (int k = k_lo; k <= k_hi; k++) {
            uint16_t wide = (k < strips)
                ? (uint16_t)((uint16_t)src[k * glyph_h] << (8 - shift))
                : 0;
            uint8_t mask = (uint8_t)((wide >> 8) | carry);
            carry = wide & 0xFF;
 
            int b = base_byte + k;
            if (b < start_byte) continue;
            if (b == start_byte) mask &= left_mask;
            if (b == end_byte)   mask &= right_mask;
            if (!mask) continue;
 
            if (current_mask != mask) {
                outb(VGA_GC_DATA, mask);
                current_mask = mask;
            }
            volatile uint8_t* ptr = row + b;
            dummy = *ptr;  // Latch background colours
            *ptr = colour; // Write Mode 2: colour goes to the masked pixels only
        }
    }
 
    // Restore standard bitmask register state
    outb(VGA_GC_DATA, 0xFF);
    (void)dummy;
}
