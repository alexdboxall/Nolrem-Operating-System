#include "../clipdraw_internal.h"
#include "../gfx_driver.h"

extern void BrushRectFallback(struct graphics_driver* drv, int x1, int y1, int x2, int y2, colour_t primary, colour_t seconday, uint8_t* pattern);

export void AddGraphicsFallbacksWhereNeeded(struct graphics_driver* drv) {
    if (drv->brush_rect == NULL) {
        drv->brush_rect = BrushRectFallback;
    }
    (void) drv;
}