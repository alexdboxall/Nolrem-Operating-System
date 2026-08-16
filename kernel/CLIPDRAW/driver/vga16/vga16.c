#include "vga16.h"

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
    volatile uint8_t* vram = VRAM_BASE;

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
            italic_offset = (height - 1 - row) >> 1;
        }

        int byte_x = (x + italic_offset) >> 3;
        int bit_shift = (x + italic_offset) & 7;
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
    volatile uint8_t* vram = VRAM_BASE;

    outb(VGA_GC_INDEX, 0x01); outb(VGA_GC_DATA, 0x0F);
    outb(VGA_GC_INDEX, 0x00); outb(VGA_GC_DATA, color & 0x0F);
    outb(VGA_GC_INDEX, 0x05); outb(VGA_GC_DATA, 0x00);

    for (int row = 0; row < height; row++) {
        uint8_t bits = bitmap[row];
        volatile uint8_t dummy;

        int italic_offset = 0;
        if (italic) {
            italic_offset = (height - 1 - row) >> 1;
        }

        int byte_x = (x + italic_offset) >> 3;
        int bit_shift = (x + italic_offset) & 7;
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
    VGADrawRawChar(x, y + 1, color, GetVgaGlyph(c), 14, false);
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

    *pre = (*pre * pt + 7) >> 3;
    *post = (*post * pt + 7) >> 3;
}


void VGADrawScaled(int x, int y, int pt, uint8_t color, char c, bool italic) {
    if (c < 32 || c >= 127 || pt <= 0) {
        return;
    }

    // 1:1 scale fast path
    if (pt == 8) {
        VGADrawRawChar(x, y + 1, color, GetVgaGlyph(c), 14, italic);
        return;
    }

    uint8_t* src_glyph = GetVgaGlyph(c);

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

static int curx = 16;
static int cury = 16 * 2;

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
            VgaScrollRect(NULL, 0, 0, 640, 480, 16);
            cury -= 16;
        }
    }
}

uint32_t VGAGetPixel(struct graphics_driver*, int x, int y) {
    if (x < 0 || x >= 640 || y < 0 || y >= 480) {
        return 0xFF000000; // Return solid black for out-of-bounds
    }

    int byte_offset = (y * 80) + (x >> 3);
    uint8_t bit_mask = 1 << (7 - (x & 7));
    
    volatile uint8_t* screen_base = (volatile uint8_t*)0xC00A0000;
    volatile uint8_t* ptr = screen_base + byte_offset;

    uint8_t color_index = 0;

    // Read 1 bit from each of the 4 color planes
    for (int plane = 0; plane < 4; plane++) {
        // Set Read Map Select register (0x04) to the current plane
        outb(0x3CE, 0x04);
        outb(0x3CF, plane);
        
        // Read the byte for this plane
        if (*ptr & bit_mask) {
            color_index |= (1 << plane);
        }
    }

    outb(0x3CE, 0x04);
    outb(0x3CF, 0x00);

    return ConvertVgaColourToARGB(color_index);
}

struct graphics_capabilities VGAGetCapabilities(struct graphics_driver*) {
    struct graphics_capabilities caps;
    caps.screen_w_px = 640;
    caps.screen_h_px = 480;
    caps.bits_per_pixel = 4;
    caps.screen_w_mm = 169;
    caps.screen_h_mm = 127;
    caps.flags = GFXCAPS_CAN_READPIXELS;
    strcpy(caps.name, "VGA 16-colour");
    return caps;
}

void InitVga() {
    ReadVgaGlyphs();
    SwitchToMode12h();
    InitVgaPalette();

    struct graphics_driver drv;
    drv.log = VGALog;
    drv.panic = VGAPanic;
    drv.fill_rect = VgaPutSolidRect;
    drv.brush_rect = VgaPutBrushRect;
    drv.invert_rect = VgaInvertRect;
    drv.scroll_rect = VgaScrollRect;
    drv.read_pixel = VGAGetPixel;
    drv.get_capabilities = VGAGetCapabilities;

    RegisterPrimaryGraphicsDriver(drv);
    
    VgaPutSolidRect(NULL, 0, 0, 640, 480, BlackColour());
    VGADrawString(16, 16, 8, 0xF, "Nolrem", true, false);
}