#pragma once

#include <stdint.h>

#define GFXCAPS_CAN_READPIXELS  (1 << 0)

#define MOUSE_BUFFER_FULL_ARGB  0
#define MOUSE_BUFFER_UINT8      1
#define MOUSE_BUFFER_UINT16     2

struct graphics_capabilities {
    char name[32];
    uint16_t screen_w_mm;
    uint16_t screen_h_mm;
    uint16_t screen_w_px;
    uint16_t screen_h_px;
    uint32_t bits_per_pixel : 8;
    uint32_t desired_mouse_restore_buffer_mode : 2;
    uint32_t flags : 22;
};

struct graphics_driver {
    void* mouse_restore_buffer;      // ALLOCATED BY KERNEL THE FIRST TIME IT IS USED
    void (*fill_rect)(struct graphics_driver*, int x1, int y1, int x2, int y2, uint32_t col);
    void (*brush_rect)(struct graphics_driver*, int x1, int y1, int x2, int y2, uint32_t primary, uint32_t secondary, uint8_t* pattern);
    void (*invert_rect)(struct graphics_driver*, int x1, int y1, int x2, int y2, bool half);
    void (*scroll_rect)(struct graphics_driver*, int x1, int y1, int x2, int y2, int delta_y);
    uint32_t (*read_pixel)(struct graphics_driver*, int x, int y);
    struct graphics_capabilities (*get_capabilities)(struct graphics_driver*);
    void (*log)(char c);
    void (*panic)(const char* s);
    void (*draw_char)(struct graphics_driver*, int x, int y, int bndx1, int bndy1, int bndx2, int bndy2, const char* s, int pt, bool bold, bool italic, uint32_t col);    
    void (*thin_line)(struct graphics_driver*, int x1, int y1, int x2, int y2, uint32_t colour);
    void (*solid_line)(struct graphics_driver*, int x1, int y1, int x2, int y2, uint32_t colour, int thickness);
    void (*pen_line)(struct graphics_driver*, int x1, int y1, int x2, int y2, uint32_t colour, int thickness, uint8_t* pattern, int pat_width, int pat_height, bool inv_instead);
    void (*draw_mouse)(struct graphics_driver*, int x, int y, const uint32_t* black, const uint32_t* white, void* restore_buffer, int width, int height);
    void (*remove_mouse)(struct graphics_driver*, int x, int y, void* _restore_buffer, int width, int height);
};
