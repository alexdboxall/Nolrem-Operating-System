#include "../api.h"
#include "../clipdraw_internal.h"
#include "../dynamic_array.h"

#define FP_SHIFT            8
#define TO_FP(x)            ((int32_t)(x) << FP_SHIFT)
#define FROM_FP(x)          (((x) + (1 << (FP_SHIFT-1))) >> FP_SHIFT)
#define FLATNESS_THRESHOLD  (1 << 10)

#define RECURSION_LIMIT     8

struct path_entry {
    int x;
    int y;
};

export pageable struct path CdCreatePath(int start_x, int start_y) {
    struct dynamic_array* data = AllocHeap(sizeof(struct dynamic_array));
    struct dynamic_array* count_data = AllocHeap(sizeof(struct dynamic_array));

    *data = CreateDynamicArray(sizeof(struct path_entry), 8);
    *count_data = CreateDynamicArray(sizeof(int), 4);

    struct path p = (struct path) {
        .x = start_x,
        .y = start_y,
        .data = (void*) data,
        .count_data = (void*) count_data,
        .num_points_total = 1,
        .polygons = 1,
        .count_total_so_far = 0,
        .scale_x_16_16 = 65536,
        .scale_y_16_16 = 65536,
        .trans_x = 0,
        .trans_y = 0
    };

    return p;
}

export pageable void CdSetPathTransform(
    struct path* p, 
    int scale_x_16_16, int scale_y_16_16, 
    int trans_x, int trans_y
) {
    p->scale_x_16_16 = scale_x_16_16;
    p->scale_y_16_16 = scale_y_16_16;
    p->trans_x = trans_x;
    p->trans_y = trans_y;
}

export pageable void CdLiftPath(struct path* p, int start_x, int start_y) {
    /* 
     * Must take the count now, otherwise the DrawPath() will cause the current
     * shape to have 1 too many, and the 'new' shape to have 1 too few.
     */
    int count = p->num_points_total - p->count_total_so_far;

    CdDrawPath(p, start_x, start_y);
    InsertDynamicArray((struct dynamic_array*) p->count_data, &count);
    p->count_total_so_far = p->num_points_total;
    p->polygons++;
}

static pageable int Transform(int64_t pos, int scale_16_16, int trans) {
    pos *= scale_16_16;
    pos /= 256;
    pos += trans;
    return (int) pos;
} 

export pageable struct region CdClosePath(struct path p, int mode) {
    struct region rgn;
        
    if (p.num_points_total < 3) {
        rgn = CdEmptyRegion();

    } else {
        int* xs = AllocHeap(p.num_points_total * sizeof(int));
        int* ys = AllocHeap(p.num_points_total * sizeof(int));
        int* counts = AllocHeap(p.polygons * sizeof(int));
        int* c_arr = DA_GET_ARRAY(*((struct dynamic_array*) p.count_data));
        int i = 0;
        for (; i < p.polygons - 1; ++i) {
            counts[i] = c_arr[i];
        }

        /* 
         * Must add one for the final (p.x, p.y) point.
         */
        counts[i] = p.num_points_total - p.count_total_so_far + 1;

        struct path_entry* arr = DA_GET_ARRAY(*((struct dynamic_array*) p.data));
        for (i = 0; i < p.num_points_total - 1; ++i) {
            xs[i] = Transform(arr[i].x, p.scale_x_16_16, p.trans_x);
            ys[i] = Transform(arr[i].y, p.scale_y_16_16, p.trans_y);
        }

        xs[i] = Transform(p.x, p.scale_x_16_16, p.trans_x);
        ys[i] = Transform(p.y, p.scale_y_16_16, p.trans_y);

        rgn = CdCreatePolyPolygonRegion(xs, ys, counts, p.polygons, mode);

        FreeHeap(xs);
        FreeHeap(ys);
        FreeHeap(counts);
    }

    /* Clear the actual array data. */
    FreeDynamicArray(*((struct dynamic_array*) p.data));

    /* The pointer to the data was allocated dynamically, so clear that. */
    FreeHeap(p.data);

    return rgn;
}

export pageable struct path CopyPath(struct path old) {
    struct dynamic_array* data = AllocHeap(sizeof(struct dynamic_array));
    *data = CopyDynamicArray(*((struct dynamic_array*) old.data));

    struct path p = old;
    p.data = (void*) data;

    return p;
}

export pageable void CdDrawPath(struct path* p, int x, int y) {
    /* 
     * Save the old 'current point' into the array. The array stores all points
     * besides the most recently added.
     */
    struct path_entry data = (struct path_entry) {
        .x = p->x,
        .y = p->y
    };
    InsertDynamicArray((struct dynamic_array*) p->data, &data);

    /* 
     * Now we can set the current point. 
     */
    p->x = x;
    p->y = y;
    p->num_points_total++;
}

static pageable int32_t Midpoint(int32_t a, int32_t b) {
    return (a + b) >> 1;
}

static pageable int IsFlatEnough(
    int32_t x1, int32_t y1,
    int32_t x2, int32_t y2,
    int32_t cx, int32_t cy)
{
    int64_t dx = (int64_t)x2 - x1;
    int64_t dy = (int64_t)y2 - y1;

    int64_t cross = dx * ((int64_t)y1 - cy) - dy * ((int64_t)x1 - cx);

    if (cross < 0) {
        cross = -cross;
    }

    return cross < FLATNESS_THRESHOLD;
}

static pageable void DrawCubicBeizerPathRecursive(
    struct path* p,
    int32_t x1, int32_t y1,
    int32_t x2, int32_t y2,
    int32_t c1x, int32_t c1y,
    int32_t c2x, int32_t c2y,
    int depth)
{
    if (depth > RECURSION_LIMIT) {
        CdDrawPath(p, FROM_FP(x2), FROM_FP(y2));
        return;
    }

    if (IsFlatEnough(x1,y1,x2,y2,c1x,c1y) && IsFlatEnough(x1,y1,x2,y2,c2x,c2y)) {
        CdDrawPath(p, FROM_FP(x2), FROM_FP(y2));
        return;
    }

    int32_t x12  = Midpoint(x1, c1x);
    int32_t y12  = Midpoint(y1, c1y);

    int32_t x23  = Midpoint(c1x, c2x);
    int32_t y23  = Midpoint(c1y, c2y);

    int32_t x34  = Midpoint(c2x, x2);
    int32_t y34  = Midpoint(c2y, y2);

    int32_t x123 = Midpoint(x12, x23);
    int32_t y123 = Midpoint(y12, y23);

    int32_t x234 = Midpoint(x23, x34);
    int32_t y234 = Midpoint(y23, y34);

    int32_t x1234 = Midpoint(x123, x234);
    int32_t y1234 = Midpoint(y123, y234);

    DrawCubicBeizerPathRecursive(
        p,
        x1, y1,
        x1234, y1234,
        x12, y12,
        x123, y123,
        depth + 1
    );
    DrawCubicBeizerPathRecursive(
        p,
        x1234, y1234,
        x2, y2,
        x234, y234,
        x34, y34,
        depth + 1
    );
}

export pageable void DrawCubicBeizerPath(struct path* p, int x, int y, int c1x, int c1y, int c2x, int c2y) {
    int x1 = p->x;
    int y1 = p->y;
    
    DrawCubicBeizerPathRecursive(
        p,
        TO_FP(x1), TO_FP(y1),
        TO_FP(x),  TO_FP(y),
        TO_FP(c1x), TO_FP(c1y),
        TO_FP(c2x), TO_FP(c2y),
        0
    );
}

export pageable void DrawQuadraticBezierPath(
    struct path* p,
    int x, int y,
    int cx, int cy)
{
    int x0 = p->x;
    int y0 = p->y;

    int32_t P0x = TO_FP(x0);
    int32_t P0y = TO_FP(y0);
    int32_t P1x = TO_FP(cx);
    int32_t P1y = TO_FP(cy);
    int32_t P2x = TO_FP(x);
    int32_t P2y = TO_FP(y);

    int32_t C1x = P0x + (int32_t)(((int64_t)(P1x - P0x) * 2) / 3);
    int32_t C1y = P0y + (int32_t)(((int64_t)(P1y - P0y) * 2) / 3);
    int32_t C2x = P2x + (int32_t)(((int64_t)(P1x - P2x) * 2) / 3);
    int32_t C2y = P2y + (int32_t)(((int64_t)(P1y - P2y) * 2) / 3);

    DrawCubicBeizerPathRecursive(
        p,
        P0x, P0y,
        P2x, P2y,
        C1x, C1y,
        C2x, C2y,
        0
    );
}
