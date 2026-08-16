#include "../clipdraw_internal.h"
#include "../gfx_driver.h"

extern void BrushRectFallback(struct graphics_driver* drv, int x1, int y1, int x2, int y2, colour_t primary, colour_t seconday, uint8_t* pattern);
extern void PenLineFallback(struct graphics_driver*, int x1, int y1, int x2, int y2, uint32_t colour, int thickness, uint8_t* pattern, int pat_width, int pat_height);
extern void SolidLineFallback(struct graphics_driver*, int x1, int y1, int x2, int y2, uint32_t colour, int thickness);
extern void ThinLineFallback(struct graphics_driver*, int x1, int y1, int x2, int y2, uint32_t colour);

export void AddGraphicsFallbacksWhereNeeded(struct graphics_driver* drv) {
    if (drv->brush_rect == NULL) {
        drv->brush_rect = BrushRectFallback;
    }
    if (drv->pen_line == NULL) {
        drv->pen_line = PenLineFallback;
    }
    if (drv->solid_line == NULL) {
        drv->solid_line = SolidLineFallback;
    }
    if (drv->thin_line == NULL) {
        drv->thin_line = ThinLineFallback;
    }
}