#include <common.h>
#include <log.h>
#include <string.h>
#include <kgfx.h>
#include "../api.h"

static inline void outb(uint16_t port, uint8_t val) {
    __asm__ volatile ("outb %b0, %w1" : : "a"(val), "Nd"(port) : "memory");
}

static inline uint8_t inb(uint16_t port) {
    uint8_t ret;
    __asm__ volatile ("inb %w1, %b0" : "=a"(ret) : "Nd"(port) : "memory");
    return ret;
}

#define VGA_ATTR_INDEX  0x3C0
#define VGA_ATTR_DATA   0x3C0
#define VGA_MISC_WRITE  0x3C2
#define VGA_SEQ_INDEX   0x3C4
#define VGA_SEQ_DATA    0x3C5
#define VGA_CRTC_INDEX  0x3D4
#define VGA_CRTC_DATA   0x3D5
#define VGA_GC_INDEX    0x3CE
#define VGA_GC_DATA     0x3CF
#define VGA_INSTAT_READ 0x3DA

uint8_t vga_glyphs[95][14];
void ReadVGAGlyphs() {
    outb(VGA_SEQ_INDEX, 0x02); uint8_t orig_seq02 = inb(VGA_SEQ_DATA);
    outb(VGA_SEQ_INDEX, 0x04); uint8_t orig_seq04 = inb(VGA_SEQ_DATA);
    outb(VGA_GC_INDEX,  0x04); uint8_t orig_gc04  = inb(VGA_GC_DATA);
    outb(VGA_GC_INDEX,  0x05); uint8_t orig_gc05  = inb(VGA_GC_DATA);
    outb(VGA_GC_INDEX,  0x06); uint8_t orig_gc06  = inb(VGA_GC_DATA);
    outb(VGA_SEQ_INDEX, 0x02); outb(VGA_SEQ_DATA, 0x04);
    outb(VGA_SEQ_INDEX, 0x04); outb(VGA_SEQ_DATA, 0x07);
    outb(VGA_GC_INDEX, 0x04); outb(VGA_GC_DATA, 0x02); // Read Map Select = Plane 2
    outb(VGA_GC_INDEX, 0x05); outb(VGA_GC_DATA, 0x00); // Read/Write mode 0, no odd/even
    outb(VGA_GC_INDEX, 0x06); outb(VGA_GC_DATA, 0x00); // Map A0000 window, chain O/E off

    volatile uint8_t* font_vram = (volatile uint8_t*) 0xC00A0000;

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

typedef struct {
    int x, y, w, h;
} VGARect;

// Returns a byte mask (bit7=leftmost) of which columns in the byte
// starting at pixel byteX*8 fall inside [clipX0, clipX1).
static inline uint8_t VGAClipByteMask(int byteX, int clipX0, int clipX1) {
    uint8_t mask = 0;
    int base = byteX * 8;
    for (int b = 0; b < 8; b++) {
        int screenX = base + b;
        if (screenX >= clipX0 && screenX < clipX1) {
            mask |= (0x80 >> b);
        }
    }
    return mask;
}

void VGADrawRawCharBounded(int x, int y, uint8_t color, uint8_t* bitmap, int height, bool italic, struct rect clip) {
    volatile uint8_t* vram = (volatile uint8_t*) 0xC00A0000;

    int clipX0 = clip.x;
    int clipX1 = clip.x + clip.w; // exclusive
    int clipY0 = clip.y;
    int clipY1 = clip.y + clip.h; // exclusive

    outb(VGA_GC_INDEX, 0x01); outb(VGA_GC_DATA, 0x0F);
    outb(VGA_GC_INDEX, 0x00); outb(VGA_GC_DATA, color & 0x0F);
    outb(VGA_GC_INDEX, 0x05); outb(VGA_GC_DATA, 0x00);

    for (int row = 0; row < height; row++) {
        int drawY = y + row;
        if (drawY < clipY0 || drawY >= clipY1) continue; // row entirely clipped

        uint8_t bits = bitmap[row];
        volatile uint8_t dummy;

        int italic_offset = 0;
        if (italic) {
            italic_offset = (height - 1 - row) / 2;
        }

        int byte_x = (x + italic_offset) / 8;
        int bit_shift = (x + italic_offset) % 8;
        uint32_t screen_offset = (uint32_t)drawY * 80 + byte_x;

        if (bit_shift == 0) {
            uint8_t mask = bits & VGAClipByteMask(byte_x, clipX0, clipX1);
            if (mask) {
                outb(VGA_GC_INDEX, 0x08); outb(VGA_GC_DATA, mask);
                dummy = vram[screen_offset];
                vram[screen_offset] = 0xFF;
            }
        } else {
            uint8_t mask1 = (bits >> bit_shift) & VGAClipByteMask(byte_x, clipX0, clipX1);
            if (mask1) {
                outb(VGA_GC_INDEX, 0x08); outb(VGA_GC_DATA, mask1);
                dummy = vram[screen_offset];
                vram[screen_offset] = 0xFF;
            }

            uint8_t mask2 = (uint8_t)(bits << (8 - bit_shift)) & VGAClipByteMask(byte_x + 1, clipX0, clipX1);
            if (mask2) {
                outb(VGA_GC_INDEX, 0x08); outb(VGA_GC_DATA, mask2);
                dummy = vram[screen_offset + 1];
                vram[screen_offset + 1] = 0xFF;
            }
        }
        (void) dummy;
    }

    outb(VGA_GC_INDEX, 0x01); outb(VGA_GC_DATA, 0x00);
    outb(VGA_GC_INDEX, 0x08); outb(VGA_GC_DATA, 0xFF);
}

void VGADrawRawChar(int x, int y, uint8_t color, uint8_t* bitmap, int height, bool italic) {
    volatile uint8_t* vram = (volatile uint8_t*) 0xC00A0000;

    outb(VGA_GC_INDEX, 0x01); outb(VGA_GC_DATA, 0x0F);
    outb(VGA_GC_INDEX, 0x00); outb(VGA_GC_DATA, color & 0x0F);
    outb(VGA_GC_INDEX, 0x05); outb(VGA_GC_DATA, 0x00);

    for (int row = 0; row < height; row++) {
        uint8_t bits = bitmap[row];
        volatile uint8_t dummy;

        int italic_offset = 0;
        if (italic) {
            italic_offset = (height - 1 - row) / 2;
        }

        int byte_x = (x + italic_offset) / 8;
        int bit_shift = (x + italic_offset) % 8;
        uint32_t screen_offset = (uint32_t)y * 80 + byte_x;

        if (bit_shift == 0) {
            outb(VGA_GC_INDEX, 0x08); outb(VGA_GC_DATA, bits);
            dummy = vram[screen_offset];
            vram[screen_offset] = 0xFF; 
        } else {
            uint8_t mask1 = bits >> bit_shift;
            if (mask1) {
                outb(VGA_GC_INDEX, 0x08); outb(VGA_GC_DATA, mask1);
                dummy = vram[screen_offset];
                vram[screen_offset] = 0xFF;
            }
            uint8_t mask2 = (uint8_t)(bits << (8 - bit_shift));
            if (mask2) {
                outb(VGA_GC_INDEX, 0x08); outb(VGA_GC_DATA, mask2);
                dummy = vram[screen_offset + 1];
                vram[screen_offset + 1] = 0xFF;
            }
        }
        (void) dummy;
        ++y;
    }

    outb(VGA_GC_INDEX, 0x01); outb(VGA_GC_DATA, 0x00);
    outb(VGA_GC_INDEX, 0x08); outb(VGA_GC_DATA, 0xFF);
}

void VGADrawChar(int x, int y, uint8_t color, char c) {
    if (c < 32 || c >= 127) {
        return;
    }
    VGADrawRawChar(x, y + 1, color, vga_glyphs[c - 32], 14, false);
}

static const uint8_t g_mode12h_seq[]  = { 0x03, 0x01, 0x0F, 0x00, 0x06 };
static const uint8_t g_mode12h_crtc[] = { 
    0x5F, 0x4F, 0x50, 0x82, 0x54, 0x80, 0x0B, 0x3E, 
    0x00, 0x40, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
    0xEA, 0x8C, 0xDF, 0x28, 0x00, 0xE7, 0x04, 0xE3, 0xFF 
};
static const uint8_t g_mode12h_gc[]   = { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x05, 0x0F, 0xFF };
static const uint8_t g_mode12h_attr[] = { 
    0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x14, 0x07, 
    0x38, 0x39, 0x3A, 0x3B, 0x3C, 0x3D, 0x3E, 0x3F, 
    0x01, 0x00, 0x0F, 0x00, 0x00 
};

static void SetMode12h(void) {
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

    // Reset state
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

static void GetGlyphMetrics(char c, int pt, int* pre, int* post) {
    *pre = 0;
    *post = 9;
    switch (c) {
    case '.':
        *pre = -3;
        *post = 7;
        break;
    case ':':
    case ';':
    case 'I':
        *pre = -2;
        *post = 8;
        break;
    case 'i':
        *pre = -2;
        *post = 7;
        break;
    case 'f':
    case 'k':
        *post = 7;
        break;
    case '\'':
        *post = 6;
        break;
    case ' ':
        *pre = -2;
        *post = 6;
        break;
    case 'l':
    case 's':
    case 'r':
    case 'z':
    case '+':
    case 'H':
    case 'a':
    case 'u':
        *post = 8;
        break;
    case 'M':
    case 'V':
    case 'W':
    case 'v':
        *post = 10;
        break;
    }

    *pre = (*pre * pt + 7) / 8;
    *post = (*post * pt + 7) / 8;
}

static const uint8_t bayer_4x4_masks[17][4] = {
    {0x00, 0x00, 0x00, 0x00}, // Level 0  (100% color1)
    {0x88, 0x00, 0x00, 0x00}, // Level 1
    {0x88, 0x00, 0x22, 0x00}, // Level 2
    {0x88, 0x00, 0xAA, 0x00}, // Level 3
    {0xAA, 0x00, 0xAA, 0x00}, // Level 4  (25% color2)
    {0xAA, 0x88, 0xAA, 0x00}, // Level 5
    {0xAA, 0x88, 0xAA, 0x22}, // Level 6
    {0xAA, 0x88, 0xAA, 0xAA}, // Level 7
    {0xAA, 0x55, 0xAA, 0x55}, // Level 8  (50% color2 - standard checkerboard)
    {0xEE, 0x55, 0xAA, 0x55}, // Level 9
    {0xEE, 0x55, 0xEE, 0x55}, // Level 10
    {0xEE, 0x55, 0xFF, 0x55}, // Level 11
    {0xFF, 0x55, 0xFF, 0x55}, // Level 12 (75% color2)
    {0xFF, 0xDD, 0xFF, 0x55}, // Level 13
    {0xFF, 0xDD, 0xFF, 0x77}, // Level 14
    {0xFF, 0xDD, 0xFF, 0xFF}, // Level 15
    {0xFF, 0xFF, 0xFF, 0xFF}  // Level 16 (100% color2)
};

void VGADrawRectBayer4x4(int x1, int y1, int x2, int y2, uint8_t color1, uint8_t color2, int level) {
    volatile uint8_t* vram = (volatile uint8_t*) 0xC00A0000;
    volatile uint8_t dummy;

    if (x1 >= 640 || y1 >= 480) return;
    if (x1 >= x2) { return; }
    if (y1 >= y2) { return; }
    if (x1 < 0) x1 = 0;
    if (x2 > 640) x2 = 640;   // FIX: was `>= 639` / `= 639`
    if (y1 < 0) y1 = 0;
    if (y2 > 480) y2 = 480;   // FIX: was `>= 479` / `= 479`
    if (level < 0) level = 0;
    if (level > 16) level = 16;

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

void VGADiabolicalRect(int x1, int y1, int x2, int y2,
                           uint8_t primary_color1, uint8_t primary_color2, int primary_level,
                           uint8_t secondary_color1, uint8_t secondary_color2, int secondary_level,
                           uint8_t* pattern) {
    volatile uint8_t* vram = (volatile uint8_t*) 0xC00A0000;
    volatile uint8_t dummy;

    if (x1 >= 640 || y1 >= 480) return;
    if (x1 >= x2) { return; }
    if (y1 >= y2) { return; }
    if (x1 < 0) x1 = 0;
    if (x2 > 640) x2 = 640;
    if (y1 < 0) y1 = 0;
    if (y2 > 480) y2 = 480;
    if (primary_level < 0) primary_level = 0;
    if (primary_level > 16) primary_level = 16;
    if (secondary_level < 0) secondary_level = 0;
    if (secondary_level > 16) secondary_level = 16;

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

void VGAInvertRect(struct graphics_driver*, int x1, int y1, int x2, int y2) {
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

// Assumes rgb is packed 0x00RRGGBB, 8 bits per channel
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

// Not in your provided register list -- these two are needed for DAC access
#define VGA_DAC_WRITE_INDEX 0x3C8   // "PEL Address Write Mode" register
#define VGA_DAC_DATA        0x3C9   // "PEL Data" register -- 3 sequential writes per index (R,G,B)

static inline void WaitVRetraceReset(void) {
    // Reading this port resets the AC's internal index/data flip-flop
    // to "expect index next" -- required before any AC access, since
    // the flip-flop's state isn't otherwise queryable.
    inb(VGA_INSTAT_READ);
}

void RemapVGAPalette(void) {
    // --- Step 1: force Attribute Controller palette registers 0-15 to
    // identity mapping, so AC register i always selects DAC index i.
    // Without this, the BIOS's EGA-compatibility defaults (register 6
    // -> DAC 0x14, registers 8-15 -> DAC 0x38-0x3F) stay in effect and
    // your DAC writes below silently miss most of the visible palette.
    WaitVRetraceReset();
    for (uint8_t i = 0; i < 16; i++) {
        outb(VGA_ATTR_INDEX, i);      // index (PAS bit=0 -- fine mid-sequence)
        outb(VGA_ATTR_DATA,  i);      // this register -> DAC index i
    }
    // Re-enable normal attribute controller operation (PAS=1). This is
    // an index-only write -- no data byte follows it.
    WaitVRetraceReset();
    outb(VGA_ATTR_INDEX, 0x20);

    // --- Step 2: load the actual colors into DAC indices 0-15 ---
    outb(VGA_DAC_WRITE_INDEX, 0);
    for (uint8_t i = 0; i < 16; i++) {
        outb(VGA_DAC_DATA, vga_palette[i].r >> 2);
        outb(VGA_DAC_DATA, vga_palette[i].g >> 2);
        outb(VGA_DAC_DATA, vga_palette[i].b >> 2);
    }
}

// integer perceptual weights, roughly luma-sensitivity (G > R > B)
#define WR 3
#define WG 4
#define WB 2

#define DITHER_BIAS 1000

// Assumes *level_out = 0 on entry.
void FindVGAColours(uint32_t rgb, uint8_t* col_out, uint8_t* level_out) {
    // This is far quicker for common colours.
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

    // --- Pass 1: best solid (non-dithered) match ---
    int32_t solid_err = 0x7FFFFFFF;
    uint8_t solid_i = 0;
    for (uint8_t i = 0; i < 16; i++) {
        int32_t er = tr - vga_palette[i].r;
        int32_t eg = tg - vga_palette[i].g;
        int32_t eb = tb - vga_palette[i].b;
        int32_t err = WR*er*er + WG*eg*eg + WB*eb*eb;
        if (err < solid_err) { solid_err = err; solid_i = i; }
    }

    // --- Pass 2: best genuine dither (interior blend only) ---
    int32_t dither_err = 0x7FFFFFFF;
    uint8_t dither_i = 0, dither_j = 0, dither_level = 0;

    const int MAX_LEVELS = 16;

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

            int32_t cr = pir + (level32*(int32_t)dx) / MAX_LEVELS;
            int32_t cg = pig + (level32*(int32_t)dy) / MAX_LEVELS;
            int32_t cb = pib + (level32*(int32_t)dz) / MAX_LEVELS;

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

void VGAPutRect(struct graphics_driver*, int x1, int y1, int x2, int y2, uint32_t colour) {
    uint8_t colours = 0;
    uint8_t level = 0;
    FindVGAColours(colour, &colours, &level);
    VGADrawRectBayer4x4(x1, y1, x2, y2, colours >> 4, colours & 0xF, level);
}

void VGAPutBrushRect(struct graphics_driver*, int x1, int y1, int x2, int y2, uint32_t primary,
    uint32_t secondary, uint8_t* pattern) {

    uint8_t pcolours = 0;
    uint8_t plevel = 0;
    uint8_t scolours = 0;
    uint8_t slevel = 0;
    FindVGAColours(primary, &pcolours, &plevel);
    FindVGAColours(secondary, &scolours, &slevel);

    VGADiabolicalRect(
        x1, y1, x2, y2, 
        pcolours >> 4, pcolours & 0xF, plevel,
        scolours >> 4, scolours & 0xF, slevel,
        pattern
    );
}

#define TOTAL_ROWS      480
#define BYTES_PER_ROW   80

void VGADrawScaled(int x, int y, int pt, uint8_t color, char c, bool italic) {
    if (c < 32 || c >= 127 || pt <= 0) {
        return;
    }

    // 1:1 scale fast path
    if (pt == 8) {
        VGADrawRawChar(x, y + 1, color, vga_glyphs[c - 32], 14, italic);
        return;
    }

    uint8_t* src_glyph = vga_glyphs[c - 32];

    // Calculate proportional height relative to original 14px height
    int target_height = (pt * 14) / 8;
    if (target_height <= 0) {
        target_height = 1;
    }

    #define MAX_SCALED_HEIGHT 128
    uint8_t scaled_chunk[MAX_SCALED_HEIGHT];

    int render_height = (target_height > MAX_SCALED_HEIGHT) ? MAX_SCALED_HEIGHT : target_height;
    int total_bytes = (pt + 7) / 8;

    for (int byte_idx = 0; byte_idx < total_bytes; byte_idx++) {
        int chunk_x_offset = byte_idx * 8;
        int chunk_width = pt - chunk_x_offset;
        if (chunk_width > 8) {
            chunk_width = 8;
        }

        for (int row = 0; row < render_height; row++) {
            // Midpoint rounded sampling for row (prevents edge bias without bolding)
            int src_row = (row * 14 + target_height / 2) / target_height;
            if (src_row > 13) src_row = 13;

            uint8_t src_row_bits = src_glyph[src_row];
            uint8_t dest_row_bits = 0;

            for (int col = 0; col < chunk_width; col++) {
                int target_x = chunk_x_offset + col;

                // Midpoint rounded sampling for column
                int src_x = (target_x * 8 + pt / 2) / pt;
                if (src_x > 7) src_x = 7;

                // Single point sample (restores original stem thickness)
                if (src_row_bits & (0x80 >> src_x)) {
                    dest_row_bits |= (0x80 >> col);
                }
            }

            scaled_chunk[row] = dest_row_bits;
        }

        VGADrawRawChar(x + chunk_x_offset, y + 1, color, scaled_chunk, render_height, italic);
    }
}

void VGADrawString(int x, int y, int pt, uint8_t col, const char* s, bool bold, bool italic) {
    int bold_offset = 1 + pt / 32 + pt / 16;
    for (int i = 0; s[i]; ++i) {
        int pre, post;
        GetGlyphMetrics(s[i], pt, &pre, &post);
        x += pre;
        VGADrawScaled(x, y, pt, col, s[i], italic);        
        if (bold) {
            x += bold_offset;
            VGADrawScaled(x, y, pt, col, s[i], italic);  
        }
        x += post;
        if (x >= 640 - pt) break;
    }
}

#include <stdint.h>

#define VGA_SEQ_INDEX   0x3C4
#define VGA_SEQ_DATA    0x3C5
#define VGA_GC_INDEX    0x3CE
#define VGA_GC_DATA     0x3CF

#define BYTES_PER_ROW   80

extern void outb(uint16_t port, uint8_t val);
extern uint8_t inb(uint16_t port);

// Helper to safely copy a row via VGA latches with exact pixel masking
static void copy_masked_row(volatile uint8_t* dest_row, volatile uint8_t* src_row, 
                            int start_byte, int end_byte, uint8_t start_mask, uint8_t end_mask) {
    uint8_t dummy;

    // Case 1: Window width fits inside a single byte
    if (start_byte == end_byte) {
        outb(VGA_GC_INDEX, 8);
        outb(VGA_GC_DATA, start_mask & end_mask);
        
        dummy = src_row[start_byte];      // Step 1: Load latches from source
        dest_row[start_byte] = dummy;     // Step 2: Write to dest (hardware applies bitmask)
        return;
    }

    // Case 2: Multi-byte span. Left edge byte.
    outb(VGA_GC_INDEX, 8);
    outb(VGA_GC_DATA, start_mask);
    dummy = src_row[start_byte];
    dest_row[start_byte] = dummy;

    // Middle bytes (completely unmasked, all 8 bits modifiable)
    if (end_byte - start_byte > 1) {
        outb(VGA_GC_INDEX, 8);
        outb(VGA_GC_DATA, 0xFF);
        for (int b = start_byte + 1; b < end_byte; b++) {
            dummy = src_row[b];
            dest_row[b] = dummy;
        }
    }

    // Right edge byte.
    outb(VGA_GC_INDEX, 8);
    outb(VGA_GC_DATA, end_mask);
    dummy = src_row[end_byte];
    dest_row[end_byte] = dummy;
}

// Helper to safely clear a row using VGA Write Mode 0 with exact pixel masking
static void clear_masked_row(volatile uint8_t* dest_row, int start_byte, int end_byte, 
                             uint8_t start_mask, uint8_t end_mask) {
    if (start_byte == end_byte) {
        outb(VGA_GC_INDEX, 8);
        outb(VGA_GC_DATA, start_mask & end_mask);
        dest_row[start_byte] = 0;
        return;
    }

    outb(VGA_GC_INDEX, 8);
    outb(VGA_GC_DATA, start_mask);
    dest_row[start_byte] = 0;

    if (end_byte - start_byte > 1) {
        outb(VGA_GC_INDEX, 8);
        outb(VGA_GC_DATA, 0xFF);
        for (int b = start_byte + 1; b < end_byte; b++) {
            dest_row[b] = 0;
        }
    }

    outb(VGA_GC_INDEX, 8);
    outb(VGA_GC_DATA, end_mask);
    dest_row[end_byte] = 0;
}

void VGAScrollRect(struct graphics_driver*, int x_start, int y_start, int x_end, int y_end, int delta_y) {
    volatile uint8_t* vram = (volatile uint8_t*) 0xC00A0000;

    if (delta_y == 0) return;
    if (x_start < 0) x_start = 0;
    if (x_end > 639) x_end = 639;
    if (y_start < 0) y_start = 0;
    if (y_end > 479) y_end = 479;
    if (x_start > x_end || y_start > y_end) return;

    int start_byte = x_start / 8;
    int end_byte   = x_end / 8;

    // Generate accurate edge masks based on pixel offsets
    uint8_t start_mask = 0xFF >> (x_start % 8);
    uint8_t end_mask   = 0xFF << (7 - (x_end % 8));

    int window_height = (y_end - y_start) + 1;

    // Back up the current Graphics Controller mode configuration
    outb(VGA_GC_INDEX, 5);
    uint8_t orig_mode = inb(VGA_GC_DATA);

    // If scroll distance matches or exceeds height, clear the precise region
    if (delta_y >= window_height || delta_y <= -window_height) {
        outb(VGA_SEQ_INDEX, 2);  outb(VGA_SEQ_DATA, 0x0F);          // Enable all planes
        outb(VGA_GC_INDEX, 5);   outb(VGA_GC_DATA, orig_mode & ~3); // Write Mode 0
        
        for (int y = y_start; y <= y_end; y++) {
            clear_masked_row(vram + (y * BYTES_PER_ROW), start_byte, end_byte, start_mask, end_mask);
        }
        outb(VGA_GC_INDEX, 8);   outb(VGA_GC_DATA, 0xFF);          // Reset mask
        return;
    }

    // Direct sequencer to open writes to all 4 video planes
    outb(VGA_SEQ_INDEX, 2);
    outb(VGA_SEQ_DATA, 0x0F);

    if (delta_y > 0) {
        // --- SCROLL UP ---
        // Copy rows top-to-bottom using Write Mode 1
        outb(VGA_GC_INDEX, 5);
        outb(VGA_GC_DATA, (orig_mode & ~3) | 1);

        for (int y = y_start; y <= (y_end - delta_y); y++) {
            volatile uint8_t* dest_row = vram + (y * BYTES_PER_ROW);
            volatile uint8_t* src_row  = vram + ((y + delta_y) * BYTES_PER_ROW);
            copy_masked_row(dest_row, src_row, start_byte, end_byte, start_mask, end_mask);
        }

        // Clear newly exposed bottom rows using Write Mode 0
        outb(VGA_GC_INDEX, 5);
        outb(VGA_GC_DATA, orig_mode & ~3);

        for (int y = (y_end - delta_y) + 1; y <= y_end; y++) {
            clear_masked_row(vram + (y * BYTES_PER_ROW), start_byte, end_byte, start_mask, end_mask);
        }
    } 
    else {
        // --- SCROLL DOWN ---
        int abs_delta = -delta_y;

        // Copy rows bottom-to-top using Write Mode 1
        outb(VGA_GC_INDEX, 5);
        outb(VGA_GC_DATA, (orig_mode & ~3) | 1);

        for (int y = y_end; y >= (y_start + abs_delta); y--) {
            volatile uint8_t* dest_row = vram + (y * BYTES_PER_ROW);
            volatile uint8_t* src_row  = vram + ((y - abs_delta) * BYTES_PER_ROW);
            copy_masked_row(dest_row, src_row, start_byte, end_byte, start_mask, end_mask);
        }

        // Clear newly exposed top rows using Write Mode 0
        outb(VGA_GC_INDEX, 5);
        outb(VGA_GC_DATA, orig_mode & ~3);

        for (int y = y_start; y < (y_start + abs_delta); y++) {
            clear_masked_row(vram + (y * BYTES_PER_ROW), start_byte, end_byte, start_mask, end_mask);
        }
    }

    // Restore hardware register state
    outb(VGA_GC_INDEX, 8);   outb(VGA_GC_DATA, 0xFF);
    outb(VGA_GC_INDEX, 5);   outb(VGA_GC_DATA, orig_mode);
}


static int curx = 16;
static int cury = 16 * 3;

void VGAPanic(const char* s) {
    cury += 32;
    VGADrawString(16, cury, 8, 0x9, "PANIC ", true, false);
    VGADrawString(80, cury, 8, 0x7, s, false, false);
}

void VGALog(char c) {
    int pre, post;
    GetGlyphMetrics(c, 8, &pre, &post);
    curx += pre;
    VGADrawChar(curx, cury, 0x7, c);
    curx += post;
    if (curx >= 640 - 24 || c == '\n') {
        curx = 16;
        cury += 16;
        if (cury >= 460) {
            VGAScrollRect(NULL, 0, 0, 640, 480, 16);
            cury -= 16;
        }
    }
}

void VGAGetParameters(struct graphics_driver*, int* sx, int* sy, int* rx, int* ry, int* bpp) {
    *sx = 640;
    *sy = 480;
    *bpp = 4;
    *rx = -1;
    *ry = -1;
}

void InitVga() {
    ReadVGAGlyphs();
    SetMode12h();
    RemapVGAPalette();

    struct graphics_driver drv;
    drv.log = VGALog;
    drv.panic = VGAPanic;
    drv.fill_rect = VGAPutRect;
    drv.get_parameters = VGAGetParameters;
    drv.invert_rect = VGAInvertRect;
    drv.scroll_rect = VGAScrollRect;
    drv.brush_rect = VGAPutBrushRect;
    
    RegisterPrimaryGraphicsDriver(drv);
    
    VGAPutRect(NULL, 0, 0, 640, 480, 0x000000);
    VGADrawString(16, 16, 8, 0xF, "Nolrem", true, false);
}