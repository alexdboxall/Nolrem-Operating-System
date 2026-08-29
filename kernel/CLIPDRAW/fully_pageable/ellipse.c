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

        int b2 = b * b;
        int dy2 = dy * dy;
        int remaining = b2 - dy2;

        if (remaining < 0) {
            AddScanline(&rgn, &ctxt, 0, NULL, false);
            continue;
        }

        // the '65536' is here to give a bit more precision to the endevour
        // the old appraoch was to put a*a*... in the dx2_scaled, but that would
        // be a*a*(a thing computed based on b*b) which may overflow 32 bits
        // and we don't want to have to do 64 bit division.
        //
        // the LL is necessary as we do want a 64 bit intermediate before the b2
        // divides it back out
        //
        // we know basically that remaining <= b2, so the most it can return
        // is the scale value
        int dx2_scaled = (int)((1073741824LL * remaining) / b2);
        int dx = a * IntegerSqrt(dx2_scaled) >> 15;

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
