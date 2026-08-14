#include "../api.h"
#include "../region_internal.h"

export pageable struct region CdCreateEllipseRegion(int x, int y, int width, int height) {
    struct region rgn = {0};
    struct region_build_context ctxt = {0};
    
    BuildNewRegion(&rgn, (int16_t) y, &ctxt);

    if (width <= 0 || height <= 0) {
        FinishRegion(&rgn, &ctxt);
        return rgn;
    }

    int a = width / 2;
    int b = height / 2;
    int center_x = x + a;
    int center_y = y + b;

    for (int i = 0; i < height; i++) {
        int cur_y = y + i;
        int dy = cur_y - center_y;

        long b2 = (long)b * b;
        long dy2 = (long)dy * dy;
        long remaining = b2 - dy2;

        if (remaining < 0) {
            AddScanline(&rgn, &ctxt, 0, NULL, false);
            continue;
        }

        // the '4096' is here to give a bit more precision to the endevour
        // the old appraoch was to put a*a*... in the dx2_scaled, but that would
        // be a*a*(a thing computed based on b*b) which may overflow 32 bits
        // and we don't want to have to do 64 bit division.
        long dx2_scaled = (4096 * remaining) / b2;
        int dx = a * IntegerSqrt((int)dx2_scaled) / 64;

        if (dx == 0) {
            /* 
             * Empty line.
             */
            AddScanline(&rgn, &ctxt, 0, NULL, false);

        } else {
            int16_t spans[2];
            spans[0] = (int16_t)(center_x - dx);
            spans[1] = (int16_t)(center_x + dx);
            AddScanline(&rgn, &ctxt, 1, spans, false);
        }
    }

    FinishRegion(&rgn, &ctxt);
    return rgn;
}
