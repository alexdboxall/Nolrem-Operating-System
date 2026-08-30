#include "vga16.h"

// Sentinel stored in a merged dither tile to mean "don't touch this
// pixel". Never returned by GetDither itself (which only ever emits
// palette indices 0-15) - it's introduced by VgaPutBrushRect when one of
// its two colours is fully transparent.
#define DITHER_NONE 0xFFu

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

// One colour pass needed to render a row of a dither tile: draw `colour`
// into every column whose bit is set in `column_mask` (bit7 = leftmost
// column of the 8-wide repeat, matching the left/right edge masks used
// throughout this file).
typedef struct {
    uint8_t colour;
    uint8_t column_mask;
} VgaDitherPass;

// Works out the passes needed to render one 8-wide row of a dither tile
// (`row` holds one VGA palette index, or DITHER_NONE, per column).
// DITHER_NONE columns never match any of the 16 palette indices checked
// here, so they naturally end up in no pass, i.e. left untouched. Returns
// the number of passes written into `out` (which must hold at least 8 -
// a row of 8 columns can name at most 8 distinct colours).
static int VgaDitherRowPasses(const uint8_t row[8], VgaDitherPass* out) {
    int count = 0;
    for (int colour = 0; colour < 16; colour++) {
        uint8_t mask = 0;
        for (int c = 0; c < 8; c++) {
            if (row[c] == colour) mask |= (uint8_t)(0x80 >> c);
        }
        if (mask) {
            out[count].colour = (uint8_t)colour;
            out[count].column_mask = mask;
            count++;
        }
    }
    return count;
}

// True if every one of the tile's 64 cells holds the same value (either
// one palette colour throughout, or DITHER_NONE throughout).
static int VgaDitherIsUniform(const uint8_t tile[64], uint8_t* out_colour) {
    uint8_t first = tile[0];
    for (int i = 1; i < 64; i++) {
        if (tile[i] != first) return 0;
    }
    *out_colour = first;
    return 1;
}

// General path: paints an 8x8 chunky dither tile (see GetDither) across
// a rectangle. The tile repeats every 8 pixels in both x and y, and -
// since every VRAM byte covers exactly 8 consecutive x pixels starting on
// a multiple of 8 - a single row's column mask lines up bit-for-bit with
// every byte in that row (left edge, middle, and right edge alike).
//
// Every (colour, mask) combination the loop needs is knowable up front
// from just the 8 row-phases and x1/x2 - none of it depends on which
// particular row is being drawn. So rather than reprogramming the GC on
// every scanline, this walks (phase, colour) on the outside and every
// matching row (stride 8) on the inside: each register value is set once
// and reused for however many rows share it, so outb() count stays flat
// as the rect gets taller instead of growing with its height.
static void VgaDrawDitherRect(int x1, int y1, int x2, int y2, const uint8_t tile[64]) {
    volatile uint8_t* vram = VRAM_BASE;
    volatile uint8_t dummy;

    VgaDitherPass passes[8][8];
    int pass_count[8];
    for (int phase = 0; phase < 8; phase++) {
        pass_count[phase] = VgaDitherRowPasses(tile + phase * 8, passes[phase]);
    }

    int start_byte  = x1 >> 3;
    int end_byte    = (x2 - 1) >> 3;
    uint8_t left_mask  = 0xFF >> (x1 & 7);
    uint8_t right_mask = 0xFF << (7 - ((x2 - 1) & 7));
    int single_byte = (start_byte == end_byte);
    int has_middle  = (!single_byte) && (start_byte + 1 < end_byte);

    // Smallest y >= y1 for each of the 8 row-phases (y & 7), so the loop
    // below can walk straight from one matching row to the next (stride
    // 8) instead of testing every row's phase individually.
    int first_y[8];
    for (int phase = 0; phase < 8; phase++) {
        first_y[phase] = y1 + ((phase - (y1 & 7) + 8) & 7);
    }

    outb(VGA_GC_INDEX, 0x05); outb(VGA_GC_DATA, 0x00); // Write mode 0
    outb(VGA_GC_INDEX, 0x01); outb(VGA_GC_DATA, 0x0F); // Enable set/reset, all planes

    // Colour (Set/Reset, index 0) and Bit Mask (index 8) are the only two
    // registers this loop touches, and neither depends on y beyond which
    // of the 8 phases a row falls into. So instead of reprogramming both
    // on every scanline, loop (phase, colour) on the outside and walk
    // just the matching rows (stride 8) on the inside - each register
    // value then gets set once and reused across every row that needs
    // it, no matter how tall the rect is. The two values are also cached
    // across the whole function so a repeat of the same colour or mask -
    // which happens for every byte-aligned rect, where the left/middle/
    // right masks for a pass are all identical - costs no outb() at all.
    int current_colour = -1;
    int current_mask   = -1;

    for (int phase = 0; phase < 8; phase++) {
        if (first_y[phase] >= y2) continue; // no rows at this phase in range

        for (int p = 0; p < pass_count[phase]; p++) {
            uint8_t colour      = passes[phase][p].colour;
            uint8_t column_mask = passes[phase][p].column_mask;

            uint8_t edge_mask = single_byte ? (uint8_t)(left_mask & right_mask & column_mask) : 0;
            uint8_t lm        = single_byte ? 0 : (uint8_t)(left_mask & column_mask);
            uint8_t mm        = has_middle  ? column_mask : 0;
            uint8_t rm        = single_byte ? 0 : (uint8_t)(right_mask & column_mask);

            if (!edge_mask && !lm && !mm && !rm) continue; // clipped away entirely

            if (current_colour != colour) {
                outb(VGA_GC_INDEX, 0x00); outb(VGA_GC_DATA, colour);
                current_colour = colour;
            }

            if (single_byte) {
                if (edge_mask) {
                    if (current_mask != edge_mask) {
                        outb(VGA_GC_INDEX, 0x08); outb(VGA_GC_DATA, edge_mask);
                        current_mask = edge_mask;
                    }
                    for (int y = first_y[phase]; y < y2; y += 8) {
                        volatile uint8_t* ptr = vram + (uint32_t)y * BYTES_PER_ROW + start_byte;
                        dummy = *ptr; *ptr = 0xFF;
                    }
                }
                continue;
            }

            if (lm) {
                if (current_mask != lm) {
                    outb(VGA_GC_INDEX, 0x08); outb(VGA_GC_DATA, lm);
                    current_mask = lm;
                }
                for (int y = first_y[phase]; y < y2; y += 8) {
                    volatile uint8_t* ptr = vram + (uint32_t)y * BYTES_PER_ROW + start_byte;
                    dummy = *ptr; *ptr = 0xFF;
                }
            }

            if (mm) {
                if (current_mask != mm) {
                    outb(VGA_GC_INDEX, 0x08); outb(VGA_GC_DATA, mm);
                    current_mask = mm;
                }
                for (int y = first_y[phase]; y < y2; y += 8) {
                    volatile uint8_t* ptr = vram + (uint32_t)y * BYTES_PER_ROW + start_byte + 1;
                    for (int b = start_byte + 1; b < end_byte; b++) { dummy = *ptr; *ptr++ = 0xFF; }
                }
            }

            if (rm) {
                if (current_mask != rm) {
                    outb(VGA_GC_INDEX, 0x08); outb(VGA_GC_DATA, rm);
                    current_mask = rm;
                }
                for (int y = first_y[phase]; y < y2; y += 8) {
                    volatile uint8_t* ptr = vram + (uint32_t)y * BYTES_PER_ROW + end_byte;
                    dummy = *ptr; *ptr = 0xFF;
                }
            }
        }
    }

    outb(VGA_GC_INDEX, 0x00); outb(VGA_GC_DATA, 0x00);
    outb(VGA_GC_INDEX, 0x01); outb(VGA_GC_DATA, 0x00);
    outb(VGA_GC_INDEX, 0x08); outb(VGA_GC_DATA, 0xFF);
    (void)dummy;
}

// Dispatches to the cheap write-mode-2 fill (VgaSimpleRect) when a tile
// turns out to need only one colour - or nothing at all, if the whole
// tile is DITHER_NONE - and to the general multi-colour path otherwise.
static void VgaDrawDitherTile(int x1, int y1, int x2, int y2, const uint8_t tile[64]) {
    uint8_t flat_colour;
    if (VgaDitherIsUniform(tile, &flat_colour)) {
        if (flat_colour != DITHER_NONE) {
            VgaSimpleRect(x1, y1, x2, y2, flat_colour);
        }
        return;
    }
    VgaDrawDitherRect(x1, y1, x2, y2, tile);
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

    uint8_t tile[64];
    GetDither(colour, tile);

    VgaDrawDitherTile(x1, y1, x2, y2, tile);
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

    uint8_t primary_tile[64];
    uint8_t secondary_tile[64];
    if (primary_opaque)   GetDither(primary,   primary_tile);
    if (secondary_opaque) GetDither(secondary, secondary_tile);

    // Merge the two dithers into one tile using `pattern` as the per-pixel
    // brush selector (pattern[row], bit7 = leftmost column - same bit
    // order the mask bytes use everywhere else in this file). Whichever
    // colour isn't opaque contributes DITHER_NONE instead of a palette
    // index, so those pixels are simply never written to - the same
    // "one brush colour opaque, one transparent" behaviour the old
    // masked-write approach gave, without doing any real alpha blending.
    uint8_t tile[64];
    for (int row = 0; row < 8; row++) {
        uint8_t sel = pattern[row];
        for (int col = 0; col < 8; col++) {
            int use_primary = (sel >> (7 - col)) & 1;
            tile[row * 8 + col] = use_primary
                ? (primary_opaque   ? primary_tile[row * 8 + col]   : DITHER_NONE)
                : (secondary_opaque ? secondary_tile[row * 8 + col] : DITHER_NONE);
        }
    }

    VgaDrawDitherTile(x1, y1, x2, y2, tile);
}