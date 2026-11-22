#include "../clipdraw_internal.h"
#include "../gfx_driver.h"

extern void CdBrushRectFallback(struct graphics_driver* drv, int x1, int y1, int x2, int y2, colour_t primary, colour_t seconday, uint8_t* pattern);
extern void CdPenLineFallback(struct graphics_driver*, int x1, int y1, int x2, int y2, uint32_t colour, int thickness, uint8_t* pattern, int pat_width, int pat_height, bool inv_instead);
extern void CdSolidLineFallback(struct graphics_driver*, int x1, int y1, int x2, int y2, uint32_t colour, int thickness);
extern void CdThinLineFallback(struct graphics_driver*, int x1, int y1, int x2, int y2, uint32_t colour);

export void CdAddGraphicsFallbacksWhereNeeded(struct graphics_driver* drv) {
    if (drv->brush_rect == NULL) {
        drv->brush_rect = CdBrushRectFallback;
    }
    if (drv->pen_line == NULL) {
        drv->pen_line = CdPenLineFallback;
    }
    if (drv->solid_line == NULL) {
        drv->solid_line = CdSolidLineFallback;
    }
    if (drv->thin_line == NULL) {
        drv->thin_line = CdThinLineFallback;
    }
}