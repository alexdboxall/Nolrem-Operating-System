#include <common.h>
#include <obj.h>
#include <string.h>
#include <transfer.h>
#include <syscall.h>
#include "../api.h"
#include "../clipdraw_internal.h"

/* Same limit as CreatePolyPolygonRegion. */
#define MAX_POINTS  50000

struct paint_polygon_args {
    int* x;
    int* y;
    struct brush* brush;    /* ignored with PAINT_USE_DC_BRUSH */
    int flags;
};

/*
 * a = DC, b = pointer to a struct paint_polygon_args, points = number of points
 * (the length of both arrays), mode = POLYGON_ALTERNATE or POLYGON_WINDING.
 * Returns 0 on success, or an errno. Drawing is clipped to the DC's clip region.
 */
pageable size_t SysPaintPolygon(size_t a, size_t args_ptr, size_t points, size_t mode) {
    if (KeIsOutOfMemory()) {
        KeSetLastUserError(ENOMEM);
        return ENOMEM;
    }
    /* Negative counts become huge unsigned values, so this rejects them too. */
    if (points < 1 || points > MAX_POINTS) {
        KeSetLastUserError(EINVAL);
        return EINVAL;
    }
    if (mode != POLYGON_ALTERNATE && mode != POLYGON_WINDING) {
        KeSetLastUserError(EINVAL);
        return EINVAL;
    }

    struct paint_polygon_args args;
    struct transfer tr = KeCreateTransferReadingFromUser((const void*) args_ptr, sizeof(args), 0);
    int res = KePerformTransfer(&args, &tr, sizeof(args));
    if (res != 0) {
        KeSetLastUserError(res);
        return res;
    }
    if (args.flags & ~PAINT_USE_DC_BRUSH) {
        KeSetLastUserError(EINVAL);
        return EINVAL;
    }

    size_t bytes = sizeof(int) * points;
    int* xx = AllocHeap(bytes);
    int* yy = AllocHeap(bytes);
    if (!xx || !yy) {
        res = ENOMEM;
        goto free_points;
    }

    tr = KeCreateTransferReadingFromUser((const void*) args.x, bytes, 0);
    res = KePerformTransfer(xx, &tr, bytes);
    if (res != 0) {
        goto free_points;
    }
    tr = KeCreateTransferReadingFromUser((const void*) args.y, bytes, 0);
    res = KePerformTransfer(yy, &tr, bytes);
    if (res != 0) {
        goto free_points;
    }

    /* Regions only hold 16-bit coordinates. */
    for (size_t i = 0; i < points; ++i) {
        if (xx[i] < INT16_MIN || xx[i] > INT16_MAX || yy[i] < INT16_MIN || yy[i] > INT16_MAX) {
            res = EINVAL;
            goto free_points;
        }
    }

    struct dc* dc;
    struct brush* brush;
    res = CdAcquirePaintObjects((void*) a, args.brush, args.flags, &dc, &brush);
    if (res != 0) {
        goto free_points;
    }

    if (brush != NULL) {
        res = CdPaintPolygonWithBrush(dc, xx, yy, (int) points, (int) mode, brush);
    } else {
        res = CdPaintPolygon(dc, xx, yy, (int) points, (int) mode);
    }
    CdReleasePaintObjects(dc, brush);

free_points:
    if (xx) FreeHeap(xx);
    if (yy) FreeHeap(yy);
    if (res != 0) {
        KeSetLastUserError(res);
    }
    return res;
}

static pageableuserexec int PaintPolygonInternal(dc_t dc, int* x, int* y, int points, int mode,
    brush_t brush, int flags) {
    struct paint_polygon_args args;
    args.x = x;
    args.y = y;
    args.brush = brush;
    args.flags = flags;
    return (int) SystemCall(SYS_PaintPolygon, (size_t) dc, (size_t) &args, points, mode);
}

export pageableuserexec int PaintPolygonWithBrush(dc_t dc, int* x, int* y, int points, int mode,
    brush_t brush) {
    return PaintPolygonInternal(dc, x, y, points, mode, brush, 0);
}

export pageableuserexec int PaintPolygon(dc_t dc, int* x, int* y, int points, int mode) {
    return PaintPolygonInternal(dc, x, y, points, mode, NULL, PAINT_USE_DC_BRUSH);
}
