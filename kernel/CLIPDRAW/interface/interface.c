#include "../gfx_driver.h"
#include "../clipdraw_internal.h"

static void CdNormaliseBrushPattern(uint8_t* output, struct brush* brush) {
    for (int y = 0; y < 8; ++y) {
        uint8_t val = brush->pattern[(y + brush->origin_y) & 7];
        output[y] = (val << (brush->origin_x & 7)) | (val >> (8 - (brush->origin_x & 7)));
    }
}

export int CdActualInvertRect(struct dc* dc, int x1, int y1, int x2, int y2, bool half) {
    struct graphics_driver* drv = CdGetOutputDriver(dc);
    CdStartVideoUpdate(x1, y1, x2, y2);
    drv->invert_rect(drv, x1, y1, x2, y2, half);
    CdEndVideoUpdate(x1, y1, x2, y2);
    return 0;
}

int CdActualPaintRectWithBrush(struct dc* dc, int x1, int y1, int x2, int y2, 
    struct brush* brush) {

    struct graphics_driver* drv = CdGetOutputDriver(dc);

    KeAcquireSpinlock(&brush->lock);
    colour_t primary = brush->primary;
    colour_t secondary = brush->secondary;
    uint8_t pattern_type = brush->pattern_type;

    if (primary == secondary || pattern_type == BRUSH_PATTERN_SOLID) {
        KeReleaseSpinlock(&brush->lock);
        CdStartVideoUpdate(x1, y1, x2, y2);
        drv->fill_rect(drv, x1, y1, x2, y2, primary);
        CdEndVideoUpdate(x1, y1, x2, y2);
    } else {
        uint8_t pattern[8];
        CdNormaliseBrushPattern(pattern, brush);
        KeReleaseSpinlock(&brush->lock);
        CdStartVideoUpdate(x1, y1, x2, y2);
        drv->brush_rect(
            drv, x1, y1, x2, y2,
            primary, secondary, pattern
        );
        CdEndVideoUpdate(x1, y1, x2, y2);
    }
    return 0;
}

int CdActualBitmapBlit(struct dc* dc, struct compat_bitmap* bitmap, struct rect src, struct point dest) {
    struct graphics_driver* drv = CdGetOutputDriver(dc);
    drv->blit_compatible_bitmap(drv, bitmap, src, dest);
    return 0; 
}  