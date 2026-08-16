#pragma once

#include <stdint.h>

#define GFXCAPS_CAN_READPIXELS  (1 << 0)

struct graphics_capabilities {
    char name[32];
    uint16_t screen_w_mm;
    uint16_t screen_h_mm;
    uint16_t screen_w_px;
    uint16_t screen_h_px;
    uint32_t bits_per_pixel : 8;
    uint32_t flags : 24;
};

struct graphics_driver {
    void (*fill_rect)(struct graphics_driver*, int x1, int y1, int x2, int y2, uint32_t col);
    void (*brush_rect)(struct graphics_driver*, int x1, int y1, int x2, int y2, uint32_t primary, uint32_t secondary, uint8_t* pattern);
    void (*invert_rect)(struct graphics_driver*, int x1, int y1, int x2, int y2);
    void (*scroll_rect)(struct graphics_driver*, int x1, int y1, int x2, int y2, int delta_y);
    uint32_t (*read_pixel)(struct graphics_driver*, int x, int y);
    struct graphics_capabilities (*get_capabilities)(struct graphics_driver*);
    void (*log)(char c);
    void (*panic)(const char* s);
    void (*draw_char)(struct graphics_driver*, int x, int y, int bndx1, int bndy1, int bndx2, int bndy2, const char* s, int pt, bool bold, bool italic, uint32_t col);    
    void (*thin_line)(struct graphics_driver*, int x1, int y1, int x2, int y2, uint32_t colour);
    void (*solid_line)(struct graphics_driver*, int x1, int y1, int x2, int y2, uint32_t colour, int thickness);
    void (*pen_line)(struct graphics_driver*, int x1, int y1, int x2, int y2, uint32_t colour, int thickness, uint8_t* pattern, int pat_width, int pat_height, bool inv_instead);
};
