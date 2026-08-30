#include "../clipdraw_internal.h"
#include "../gfx_driver.h"

#include <log.h>

#define MIN4(a,b,c,d) MIN(MIN(a,b), MIN(c,d))
#define MAX4(a,b,c,d) MAX(MAX(a,b), MAX(c,d))

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

static void SetPenPixel(struct graphics_driver* drv, int x, int y, int u, int v, uint32_t colour, uint8_t* pattern, int pat_width, int pat_height, bool inv_instead_of_col) {
    if (GetPatternBitHorizontal(u, v, pattern, pat_width, pat_height)) {
        CdStartVideoUpdate(x, y, x + 1, y + 1);
        if (inv_instead_of_col) {
            drv->invert_rect(drv, x, y, x + 1, y + 1);
        } else {
            drv->fill_rect(drv, x, y, x + 1, y + 1, colour);
        }
        CdEndVideoUpdate(x, y, x + 1, y + 1);
    }
}

void PenLineFallback(struct graphics_driver* drv, int x1, int y1, int x2, int y2, 
    uint32_t colour, int thickness, uint8_t* pattern, 
    int pat_width, int pat_height, bool inv_instead_of_col
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

    // 2. Compute normal vectors scaled by thickness with perfect integer rounding.
    //
    // NOTE on odd thickness: this offset formula rounds thickness/2 to the
    // nearest integer, which is EXACT whenever thickness is even but rounds
    // *up* whenever thickness is odd (thickness/2 has a .5 remainder and the
    // rounding here goes away from zero). If we mirrored a single rounded
    // half-offset onto both sides of the line like the old code did, the
    // total width would be 2x that rounded value -- always even, so every
    // odd thickness silently became the next even number up (1->2, 3->4,
    // 5->6, ...).
    //
    // Fix: don't mirror one offset. Round thickness up to the nearest even
    // number for one side of the line and down to the nearest even number
    // for the other side; feed each independently through the same (exact
    // for even inputs) formula below. For even thickness both sides get the
    // same value back, so behaviour is unchanged. For odd thickness the two
    // sides differ by 2, i.e. their offsets differ by 1 pixel, and together
    // they add up to exactly the requested thickness instead of one too many.
    int t_neg = thickness - (thickness & 1); // thickness rounded down to even
    int t_pos = thickness + (thickness & 1); // thickness rounded up to even

    int div = 2 * len;

    int oxp_num = -dy * t_pos;
    int oyp_num = dx * t_pos;
    int oxn_num = -dy * t_neg;
    int oyn_num = dx * t_neg;

    int oxp = (oxp_num >= 0 ? (oxp_num + len) : (oxp_num - len)) / div;
    int oyp = (oyp_num >= 0 ? (oyp_num + len) : (oyp_num - len)) / div;
    int oxn = (oxn_num >= 0 ? (oxn_num + len) : (oxn_num - len)) / div;
    int oyn = (oyn_num >= 0 ? (oyn_num + len) : (oyn_num - len)) / div;

    // 3. Compute the 4 integer corners of the parallelogram bounding envelope.
    // p1/p4 sit on the "+t_pos" side, p2/p3 on the "-t_neg" side -- for even
    // thickness this is identical to the old symmetric +ox/-ox placement.
    int p1x = x1 + oxp; int p1y = y1 + oyp;
    int p2x = x1 - oxn; int p2y = y1 - oyn;
    int p3x = x2 - oxn; int p3y = y2 - oyn;
    int p4x = x2 + oxp; int p4y = y2 + oyp;

    // 4. Track down the Axis-Aligned Bounding Box (AABB)
    int minX = MIN4(p1x, p2x, p3x, p4x);
    int minY = MIN4(p1y, p2y, p3y, p4y);
    int maxX = MAX4(p1x, p2x, p3x, p4x);
    int maxY = MAX4(p1y, p2y, p3y, p4y);

    // Half-thickness on the "-t_neg" side, used below to shift the pattern's
    // V coordinate so it still starts at 0 at the p2/p3 edge now that the
    // two sides can have different offsets.
    int half_neg = t_neg / 2;

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
                
                // Offset V by the "-t_neg" side's half-thickness so the pattern
                // still wraps linearly from 0 to thickness-1 starting at the
                // p2/p3 edge, instead of split mirrored indexing.
                int v = v_raw + half_neg;

                SetPenPixel(drv, x, y, u, v, colour, pattern, pat_width, pat_height, inv_instead_of_col);
            }
        }
    }
}