#include "../clipdraw_internal.h"
#include "../gfx_driver.h"

#include <log.h>

extern int IntegerSqrt(int x);

static void DrawThinLine(struct graphics_driver* drv, int x1, int y1, int x2, int y2, uint32_t colour) {
    int dx =  x2 - x1; if (dx < 0) dx = -dx;
    int dy =  y2 - y1; if (dy < 0) dy = -dy;
    int sx = x1 < x2 ? 1 : -1;
    int sy = y1 < y2 ? 1 : -1;
    int err = dx - dy;

    while (1) {
        drv->fill_rect(drv, x1, y1, x1 + 1, y1 + 1, colour);

        if (x1 == x2 && y1 == y2) break;
        int e2 = 2 * err;
        if (e2 > -dy) {
            err -= dy;
            x1 += sx;
        }
        if (e2 < dx) {
            err += dx;
            y1 += sy;
        }
    }
}

static inline int EdgeFunction32(int ax, int ay, int bx, int by, int px, int py) {
    return (bx - ax) * (py - 2 * ay) - (by - ay) * (px - 2 * ax);
}

static void DrawThickLineSolid(struct graphics_driver* drv, int x1, int y1, int x2, int y2, uint32_t colour, int thickness) {
    int dx = x2 - x1;
    int dy = y2 - y1;
    int len;

    int abs_dx = dx < 0 ? -dx : dx;
    int abs_dy = dy < 0 ? -dy : dy;
    if (abs_dx > 32767 || abs_dy > 32767) {
        int dx_s = dx >> 1;
        int dy_s = dy >> 1;
        len = IntegerSqrt(dx_s * dx_s + dy_s * dy_s) << 1;
    } else {
        len = IntegerSqrt(dx * dx + dy * dy);
    }

    if (len == 0) {
        drv->fill_rect(drv, x1, y1, x1 + 1, y1 + 1, colour);
        return;
    }

    int div = 2 * len;
    int ox_num = -dy * thickness;
    int oy_num = dx * thickness;
    
    int ox = (ox_num >= 0 ? (ox_num + len) : (ox_num - len)) / div;
    int oy = (oy_num >= 0 ? (oy_num + len) : (oy_num - len)) / div;

    int p1x = x1 + ox; int p1y = y1 + oy;
    int p2x = x1 - ox; int p2y = y1 - oy;
    int p3x = x2 - ox; int p3y = y2 - oy;
    int p4x = x2 + ox; int p4y = y2 + oy;

    #define MIN(a, b) ((a) < (b) ? (a) : (b))
    #define MAX(a, b) ((a) > (b) ? (a) : (b))
    #define MIN4(a,b,c,d) MIN(MIN(a,b), MIN(c,d))
    #define MAX4(a,b,c,d) MAX(MAX(a,b), MAX(c,d))

    int minX = MIN4(p1x, p2x, p3x, p4x);
    int minY = MIN4(p1y, p2y, p3y, p4y);
    int maxX = MAX4(p1x, p2x, p3x, p4x);
    int maxY = MAX4(p1y, p2y, p3y, p4y);

    for (int y = minY; y <= maxY; y++) {
        int py2 = 2 * y + 1;
        for (int x = minX; x <= maxX; x++) {
            int px2 = 2 * x + 1;

            int e1 = EdgeFunction32(p1x, p1y, p2x, p2y, px2, py2);
            int e2 = EdgeFunction32(p2x, p2y, p3x, p3y, px2, py2);
            int e3 = EdgeFunction32(p3x, p3y, p4x, p4y, px2, py2);
            int e4 = EdgeFunction32(p4x, p4y, p1x, p1y, px2, py2);

            if ((e1 >= 0 && e2 >= 0 && e3 >= 0 && e4 >= 0) ||
                (e1 <= 0 && e2 <= 0 && e3 <= 0 && e4 <= 0)) {
                drv->fill_rect(drv, x, y, x + 1, y + 1, colour);
            }
        }
    }
}

void ThinLineFallback(struct graphics_driver* drv, int x1, int y1, int x2, int y2, 
    uint32_t colour
) {
    DrawThinLine(drv, x1, y1, x2, y2, colour);
}

void SolidLineFallback(struct graphics_driver* drv, int x1, int y1, int x2, int y2, 
    uint32_t colour, int thickness
) {
    if (thickness <= 0) {
        return;
    }
    
    if (thickness == 1) {
        DrawThinLine(drv, x1, y1, x2, y2, colour);
    } else {
        DrawThickLineSolid(drv, x1, y1, x2, y2, colour, thickness);
    }
}
