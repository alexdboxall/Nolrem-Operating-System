#include "../clipdraw_internal.h"
#include "../region_internal.h"
#include "../dynamic_array.h"
#include <heap.h>

struct intersection {
    int x;
    int direction;      // +1 or -1
};

static pageable int IntersectionComparator(void* a, void* b, int n) {
    (void) n;

    struct intersection ai = *(struct intersection*)a;
    struct intersection bi = *(struct intersection*)b;

    if (ai.x < bi.x) return -1;
    if (ai.x > bi.x) return 1;

    return 0;
}

export pageable struct region CdCreatePolyPolygonRegion(int* px, int* py, int* counts, int polygons, int mode) {
    if (polygons <= 0 || !px || !py || !counts) {
        return CdEmptyRegion();
    }

    // 1. Find the global Y range
    int y_min = 32767, y_max = -32768;
    int total_points = 0;
    for (int i = 0; i < polygons; ++i) {
        if (counts[i] >= 3) {
            for (int j = 0; j < counts[i]; ++j) {
                int cur_y = py[total_points + j];
                if (cur_y < y_min) y_min = cur_y;
                if (cur_y > y_max) y_max = cur_y;
            }
        }
        total_points += counts[i];
    }

    if (y_min >= y_max) return CdEmptyRegion();

    struct region rgn = {0};
    struct region_build_context ctxt = {0};
    BuildNewRegion(&rgn, (int16_t)y_min, &ctxt);

    // Temp buffer for current scanline spans (int16_t x0, x1 pairs)
    // Max spans per line is total_points / 2
    int16_t* scanline_x = (int16_t*)AllocHeap(sizeof(int16_t) * total_points);

    // 2. Process each scanline row by row
    for (int y = y_min; y < y_max; ++y) {
        struct dynamic_array intersections = CreateDynamicArray(sizeof(struct intersection), 8);

        int poly_offset = 0;
        for (int p = 0; p < polygons; ++p) {
            if (counts[p] >= 3) {
                    for (int i = 0; i < counts[p]; ++i) {
                    int next = (i + 1) % counts[p];
                    int x0 = px[poly_offset + i], y0 = py[poly_offset + i];
                    int x1 = px[poly_offset + next], y1 = py[poly_offset + next];

                    if (y0 == y1) continue; // Skip horizontal edges

                    // Does this edge cross our scanline (y)?
                    if ((y >= y0 && y < y1) || (y >= y1 && y < y0)) {
                        struct intersection isect;
                        // Linear interpolation to find X at this integer Y
                        isect.x = x0 + (int)((long)(x1 - x0) * (y - y0) / (y1 - y0));
                        isect.direction = (y0 < y1) ? 1 : -1;
                        InsertDynamicArray(&intersections, &isect);
                    }
                }
            }
            poly_offset += counts[p];
        }

        if (intersections.used == 0) {
            AddScanline(&rgn, &ctxt, 0, NULL, false);
            FreeDynamicArray(intersections);
            continue;
        }

        SortDynamicArray(&intersections, IntersectionComparator);

        int num_spans = 0;
        if (mode == POLYGON_ALTERNATE) {
            // Even-Odd Rule
            for (int i = 0; i + 1 < intersections.used; i += 2) {
                int x_start = ((struct intersection*)DA_GET_ARRAY(intersections))[i].x;
                int x_end   = ((struct intersection*)DA_GET_ARRAY(intersections))[i+1].x;
                if (x_start > x_end) { int t = x_start; x_start = x_end; x_end = t; }
                
                if (x_start != x_end) {
                    scanline_x[num_spans * 2 + 0] = (int16_t)x_start;
                    scanline_x[num_spans * 2 + 1] = (int16_t)x_end;
                    num_spans++;
                }
            }
        } else {
            // Non-Zero Winding Rule
            int winding = 0, start_x = 0;
            struct intersection* isects = (struct intersection*)DA_GET_ARRAY(intersections);
            for (int i = 0; i < intersections.used; ++i) {
                if (winding == 0) start_x = isects[i].x;
                winding += isects[i].direction;
                if (winding == 0) {
                    int x_end = isects[i].x;
                    if (start_x > x_end) { int t = start_x; start_x = x_end; x_end = t; }
                    if (start_x < x_end) {
                        scanline_x[num_spans * 2 + 0] = (int16_t)start_x;
                        scanline_x[num_spans * 2 + 1] = (int16_t)x_end;
                        num_spans++;
                    }
                }
            }
        }

        AddScanline(&rgn, &ctxt, num_spans, scanline_x, false);
        FreeDynamicArray(intersections);
    }

    FreeHeap(scanline_x);
    FinishRegion(&rgn, &ctxt);
    return rgn;
}

export pageable struct region CdCreatePolygonRegion(int* px, int* py, int points, int mode) {
    int count = points;
    return CdCreatePolyPolygonRegion(px, py, &count, 1, mode);
}
