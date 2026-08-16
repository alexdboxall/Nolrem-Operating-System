#include "../clipdraw_internal.h"
#include "../gfx_driver.h"

#include <log.h>

static bool GetPatternBitHorizontal(int x, int y, uint8_t* pattern, int pat_width, int pat_height) {
    // Safety guard against any negative coordinates slipping through
    int x_off = (x % pat_width);
    if (x_off < 0) x_off += pat_width;
    
    int y_off = (y % pat_height);
    if (y_off < 0) y_off += pat_height;

    uint8_t byte = pattern[y_off];
    return byte & (1 << ((pat_width - 1) - x_off));
}

// Fixed standard cross-product implementation: Vector(AB) cross Vector(AM)
// ax/ay = line segment start, bx/by = line segment end, px/py = scaled pixel center (2*coord + 1)
static inline int EdgeFunction32(int ax, int ay, int bx, int by, int px, int py) {
    return (bx - ax) * (py - 2 * ay) - (by - ay) * (px - 2 * ax);
}

static void SetPenPixel(struct graphics_driver* drv, int x, int y, int u, int v, uint32_t colour, uint8_t* pattern, int pat_width, int pat_height) {
    if (GetPatternBitHorizontal(u, v, pattern, pat_width, pat_height)) {
        drv->fill_rect(drv, x, y, x + 1, y + 1, colour);
    }
}

void PenLineFallback(struct graphics_driver* drv, int x1, int y1, int x2, int y2, 
    uint32_t colour, int thickness, uint8_t* pattern, 
    int pat_width, int pat_height
) {
    int dx = x2 - x1;
    int dy = y2 - y1;
    int len;

    // 1. Prevent 32-bit overflow when squaring coordinates near the 16-bit boundary limit
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
        return;
    }

    // 2. Compute normal vectors scaled by thickness with perfect integer rounding
    int div = 2 * len;
    int ox_num = -dy * thickness;
    int oy_num = dx * thickness;
    
    int ox = (ox_num >= 0 ? (ox_num + len) : (ox_num - len)) / div;
    int oy = (oy_num >= 0 ? (oy_num + len) : (oy_num - len)) / div;

    // 3. Compute the 4 integer corners of the parallelogram bounding envelope
    int p1x = x1 + ox; int p1y = y1 + oy;
    int p2x = x1 - ox; int p2y = y1 - oy;
    int p3x = x2 - ox; int p3y = y2 - oy;
    int p4x = x2 + ox; int p4y = y2 + oy;

    #define MIN(a, b) ((a) < (b) ? (a) : (b))
    #define MAX(a, b) ((a) > (b) ? (a) : (b))
    #define MIN4(a,b,c,d) MIN(MIN(a,b), MIN(c,d))
    #define MAX4(a,b,c,d) MAX(MAX(a,b), MAX(c,d))

    // 4. Track down the Axis-Aligned Bounding Box (AABB)
    int minX = MIN4(p1x, p2x, p3x, p4x);
    int minY = MIN4(p1y, p2y, p3y, p4y);
    int maxX = MAX4(p1x, p2x, p3x, p4x);
    int maxY = MAX4(p1y, p2y, p3y, p4y);

    // 5. Query the bounding box
    for (int y = minY; y <= maxY; y++) {
        int py2 = 2 * y + 1;
        for (int x = minX; x <= maxX; x++) {
            int px2 = 2 * x + 1;

            // Clean, structural edge loop checks using explicit point-pairs
            int e1 = EdgeFunction32(p1x, p1y, p2x, p2y, px2, py2);
            int e2 = EdgeFunction32(p2x, p2y, p3x, p3y, px2, py2);
            int e3 = EdgeFunction32(p3x, p3y, p4x, p4y, px2, py2);
            int e4 = EdgeFunction32(p4x, p4y, p1x, p1y, px2, py2);

            // If the pixel center sits consistently inside all bounds, draw it
            if ((e1 >= 0 && e2 >= 0 && e3 >= 0 && e4 >= 0) ||
                (e1 <= 0 && e2 <= 0 && e3 <= 0 && e4 <= 0)) {
                
                // --- PATTERN SPACE MAPPING ---
                int mx2 = px2 - 2 * x1;
                int my2 = py2 - 2 * y1;

                // 1. Calculate distance ALONG the line (U coordinate)
                int32_t dot_product = (int32_t)mx2 * dx + (int32_t)my2 * dy;
                int u = (dot_product + len) / (2 * len);

                // 2. Calculate distance ACROSS the line (V coordinate)
                int32_t cross_product = (int32_t)mx2 * (-dy) + (int32_t)my2 * dx;
                int v_raw = (cross_product + len) / (2 * len);
                
                // Offset V by half thickness so the pattern wraps linearly from 0 to thickness-1 
                // instead of split mirrored indexing from (-thickness/2)
                int v = v_raw + (thickness / 2);

                SetPenPixel(drv, x, y, u, v, colour, pattern, pat_width, pat_height);
            }
        }
    }
}
