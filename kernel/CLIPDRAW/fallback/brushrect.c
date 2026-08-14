#include "../clipdraw_internal.h"
#include "../gfx_driver.h"

#include <log.h>

void BrushRectFallback(struct graphics_driver* drv, int x1, int y1, int x2, int y2, colour_t primary, colour_t secondary, uint8_t* pattern) {
    while (y1 < y2) {
        uint8_t row_pattern = pattern[y1 % 8];

        if (row_pattern == 0) {
            drv->fill_rect(drv, x1, y1, x2, y1 + 1, secondary);

        } else if (row_pattern == 0xFF) {
            drv->fill_rect(drv, x1, y1, x2, y1 + 1, primary);

        } else {
            for (int x = x1; x < x2; ++x) {
                colour_t col = secondary;
                if (row_pattern & (1 << (7 - (x % 8)))) {
                    col = primary;
                }
                drv->fill_rect(drv, x, y1, x + 1, y1 + 1, col);
            }
        }
        ++y1;
    }
}

