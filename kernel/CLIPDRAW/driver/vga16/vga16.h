#pragma once

#include <common.h>
#include <log.h>
#include <string.h>
#include <kgfx.h>
#include "../../api.h"

#define TOTAL_ROWS      480
#define BYTES_PER_ROW   80

#define VRAM_BASE       ((volatile uint8_t*) 0xC00A0000)

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
#define VGA_DAC_WRITE_INDEX 0x3C8
#define VGA_DAC_DATA        0x3C9

static inline void outb(uint16_t port, uint8_t val) {
    __asm__ volatile ("outb %b0, %w1" : : "a"(val), "Nd"(port) : "memory");
}

static inline uint8_t inb(uint16_t port) {
    uint8_t ret;
    __asm__ volatile ("inb %w1, %b0" : "=a"(ret) : "Nd"(port) : "memory");
    return ret;
}

void GetDither(uint32_t col, uint8_t* outbuffer);

void SwitchToMode12h(void);
void ReadVgaGlyphs(void);
uint8_t* GetVgaGlyph(char c);
void FindVgaColours(uint32_t rgb, uint8_t* col_out, uint8_t* level_out);
colour_t ConvertVgaColourToARGB(int vgacol);
void InitVgaPalette(void);

void VgaInvertRect(struct graphics_driver*, int x1, int y1, int x2, int y2, bool half);
void VgaScrollRect(struct graphics_driver*, int x_start, int y_start, int x_end, int y_end, int delta_y);
void VgaPutSolidRect(struct graphics_driver*, int x1, int y1, int x2, int y2, uint32_t colour);
void VgaPutBrushRect(struct graphics_driver*, int x1, int y1, int x2, int y2, uint32_t primary,
    uint32_t secondary, uint8_t* pattern);
void VgaPenLine(struct graphics_driver*, int x1, int y1, int x2, int y2, uint32_t colour, int thickness, uint8_t* pattern, int pat_width, int pat_height);
void VGADrawMouse(struct graphics_driver*, int x, int y, const uint32_t* black, const uint32_t* white, void* _restore_buffer, int width, int height);
void VGARemoveMouse(struct graphics_driver*, int x, int y, void* _restore_buffer, int width, int height);