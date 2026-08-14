#pragma once

#include <stdint.h>

struct graphics_driver {
    void (*fill_rect)(struct graphics_driver*, int x1, int y1, int x2, int y2, uint32_t col);
    void (*brush_rect)(struct graphics_driver*, int x1, int y1, int x2, int y2, uint32_t primary, uint32_t secondary, uint8_t* pattern);
    void (*invert_rect)(struct graphics_driver*, int x1, int y1, int x2, int y2);
    void (*scroll_rect)(struct graphics_driver*, int x1, int y1, int x2, int y2, int delta_y);
    void (*get_parameters)(struct graphics_driver*, int* screen_w, int* screen_h, int* res_x, int* res_y, int* bpp);
    void (*log)(char c);
    void (*panic)(const char* s);
    void (*draw_char)(struct graphics_driver*, int x, int y, int bndx1, int bndy1, int bndx2, int bndy2, const char* s, int pt, bool bold, bool italic, uint32_t col);    
};
