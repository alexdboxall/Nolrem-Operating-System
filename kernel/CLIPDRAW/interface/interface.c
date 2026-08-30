#include "../gfx_driver.h"
#include "../clipdraw_internal.h"

static void NormaliseBrushPattern(uint8_t* output, struct brush* brush) {
    for (int y = 0; y < 8; ++y) {
        uint8_t val = brush->pattern[(y + brush->origin_y) & 7];
        output[y] = (val << (brush->origin_x & 7)) | (val >> (8 - (brush->origin_x & 7)));
    }
}

int ActualInvertRect(struct dc* dc, int x1, int y1, int x2, int y2) {
    struct graphics_driver* drv = GetOutputDriver(dc);
    CdStartVideoUpdate(x1, y1, x2, y2);
    drv->invert_rect(drv, x1, y1, x2, y2);
    CdEndVideoUpdate(x1, y1, x2, y2);
    return 0;
}

int ActualPaintRectWithBrush(struct dc* dc, int x1, int y1, int x2, int y2, 
    struct brush* brush) {

    struct graphics_driver* drv = GetOutputDriver(dc);

    LockUserObject(brush);
    colour_t primary = brush->primary;
    colour_t secondary = brush->secondary;
    uint8_t pattern_type = brush->pattern_type;

    if (primary == secondary || pattern_type == BRUSH_PATTERN_SOLID) {
        UnlockUserObject(brush);
        CdStartVideoUpdate(x1, y1, x2, y2);
        drv->fill_rect(drv, x1, y1, x2, y2, primary);
        CdEndVideoUpdate(x1, y1, x2, y2);
    } else {
        uint8_t pattern[8];
        NormaliseBrushPattern(pattern, brush);
        UnlockUserObject(brush);
        CdStartVideoUpdate(x1, y1, x2, y2);
        drv->brush_rect(
            drv, x1, y1, x2, y2,
            primary, secondary, pattern
        );
        CdEndVideoUpdate(x1, y1, x2, y2);
    }
    return 0;
}
