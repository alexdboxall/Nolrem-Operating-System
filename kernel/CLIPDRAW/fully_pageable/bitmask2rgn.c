#include "../api.h"
#include "../region_internal.h"

export pageable struct region CdCreateRegionFromBitmask(int x, int y, int width, int height, uint8_t* data) {
    struct region rgn = {0};
    struct region_build_context ctxt = {0};

    BuildNewRegion(&rgn, (int16_t)y, &ctxt);
    if (width <= 0 || height <= 0 || data == NULL) {
        FinishRegion(&rgn, &ctxt);
        return rgn;
    }

    for (int i = 0; i < height; ++i) {
        // TODO: add scanlines
    }

        /*int16_t spans[2];
        spans[0] = (int16_t)(x + dx);
        spans[1] = (int16_t)(x + width - dx);
        AddScanline(&rgn, &ctxt, 1, spans, false);*/
    
    FinishRegion(&rgn, &ctxt);
    return rgn;
}
