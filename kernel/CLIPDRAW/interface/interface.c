#include "../gfx_driver.h"
#include "../clipdraw_internal.h"

export int CdInvertRect(struct dc* dc, int x, int y, int width, int height) {
    struct graphics_driver* drv = GetOutputDriver(dc);
    drv->invert_rect(drv, x, y, x + width, y + height);
    return 0;
}

static void NormaliseBrushPattern(uint8_t* output, struct brush* brush) {
    for (int y = 0; y < 8; ++y) {
        uint8_t val = brush->pattern[(y + brush->origin_y) & 7];
        output[y] = (val << (brush->origin_x & 7)) | (val >> (8 - (brush->origin_x & 7)));
    }
}

export int CdPaintRectWithBrush(struct dc* dc, int x, int y, int width, int height, 
    struct brush* brush) {

    struct graphics_driver* drv = GetOutputDriver(dc);

    LockUserObject(brush);
    colour_t primary = brush->primary;
    colour_t secondary = brush->secondary;
    uint8_t pattern_type = brush->pattern_type;

    if (primary == secondary || pattern_type == BRUSH_PATTERN_SOLID) {
        UnlockUserObject(brush);
        drv->fill_rect(drv, x, y, x + width, y + height, primary);
    } else {
        uint8_t pattern[8];
        NormaliseBrushPattern(pattern, brush);
        UnlockUserObject(brush);
        drv->brush_rect(
            drv, x, y, x + width, y + height,
            primary, secondary, pattern
        );
    }
    return 0;
}


