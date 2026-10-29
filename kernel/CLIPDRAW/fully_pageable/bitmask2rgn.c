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

    // Determine the number of bytes per row (row pitch / stride)
    // Assuming 1 bit per pixel, packed sequentially per row
    int bytes_per_row = (width + 7) / 8;

    for (int i = 0; i < height; ++i) {
        uint8_t* row_data = data + (i * bytes_per_row);
        int inside_span = 0;
        int span_start_x = 0;

        for (int dx = 0; dx < width; ++dx) {
            // Extract the target bit for the current x-offset
            int byte_idx = dx / 8;
            int bit_idx = 7 - (dx % 8); // Assuming MSB-to-LSB bit order
            int is_bit_set = (row_data[byte_idx] & (1 << bit_idx)) != 0;

            if (is_bit_set) {
                if (!inside_span) {
                    // Start of a new span
                    span_start_x = x + dx;
                    inside_span = 1;
                }
            } else {
                if (inside_span) {
                    // End of the current span reached (transitioning from 1 to 0)
                    int16_t spans[2];
                    spans[0] = (int16_t)span_start_x;
                    spans[1] = (int16_t)(x + dx);
                    
                    AddScanline(&rgn, &ctxt, 1, spans, false);
                    inside_span = 0;
                }
            }
        }

        // Flush any active span that runs up to the right edge of the bounding box
        if (inside_span) {
            int16_t spans[2];
            spans[0] = (int16_t)span_start_x;
            spans[1] = (int16_t)(x + width);
            
            AddScanline(&rgn, &ctxt, 1, spans, false);
        }
    }

    FinishRegion(&rgn, &ctxt);
    return rgn;
}
