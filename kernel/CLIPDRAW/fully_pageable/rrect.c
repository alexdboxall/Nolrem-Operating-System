#include "../api.h"
#include "../region_internal.h"

export pageable struct region CdCreateRoundedRectRegion(int x, int y, int width, int height, int radius) {
    struct region rgn = {0};
    struct region_build_context ctxt = {0};

    // 1. Safety check for radius
    int max_r = (width < height ? width : height) / 2;
    if (radius >= max_r) {
        radius = max_r - 1;
    }
    if (radius < 0) radius = 0;

    BuildNewRegion(&rgn, (int16_t)y, &ctxt);

    if (width <= 0 || height <= 0) {
        FinishRegion(&rgn, &ctxt);
        return rgn;
    }

    // Phase A: Top Rounded Corners
    // We treat the top two corners as part of a single circle of radius 'radius'
    for (int i = 0; i < radius; i++) {
        int dy = radius - i; // Distance from the "center" of the corner circles
        long dy2 = (long)dy * dy;
        long r2 = (long)radius * radius;
        
        // Calculate the horizontal inset using the circle equation
        int dx = radius - IntegerSqrt((int)(r2 - dy2));

        int16_t spans[2];
        spans[0] = (int16_t)(x + dx);
        spans[1] = (int16_t)(x + width - dx);
        AddScanline(&rgn, &ctxt, 1, spans, false);
    }

    // Phase B: The Straight Middle
    // This is the "meat" of the window. 
    // We call AddScanline once for every row, but your vertical merger 
    // will squash this entire block into a single massive Band!
    int16_t mid_spans[2];
    mid_spans[0] = (int16_t)x;
    mid_spans[1] = (int16_t)(x + width);
    
    int mid_height = height - (2 * radius);
    for (int i = 0; i < mid_height; i++) {
        AddScanline(&rgn, &ctxt, 1, mid_spans, i != 0);
    }

    // Phase C: Bottom Rounded Corners
    // Mirror of the top
    for (int i = 0; i < radius; i++) {
        int dy = i + 1; 
        long dy2 = (long)dy * dy;
        long r2 = (long)radius * radius;
        
        int dx = radius - IntegerSqrt((int)(r2 - dy2));

        int16_t spans[2];
        spans[0] = (int16_t)(x + dx);
        spans[1] = (int16_t)(x + width - dx);
        AddScanline(&rgn, &ctxt, 1, spans, false);
    }

    FinishRegion(&rgn, &ctxt);
    return rgn;
}
