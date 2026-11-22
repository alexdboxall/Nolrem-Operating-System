#include <common.h>
#include <obj.h>
#include <string.h>
#include <syscall.h>
#include "../api.h"
#include "../clipdraw_internal.h"

/*
 * a = DC, b = pointer to a struct user_shape_args.
 * Returns 0 on success, or an errno. Drawing is clipped to the DC's clip region.
 */
pageable size_t SysPaintRoundedRect(size_t a, size_t args_ptr, size_t, size_t) {
    if (IsOutOfMemory()) {
        KeSetLastUserError(ENOMEM);
        return ENOMEM;
    }

    struct user_shape_args args;
    int res = CdReadShapeArgs(args_ptr, &args);
    if (res == 0 && args.radius < 0) {
        res = EINVAL;
    }
    if (res != 0) {
        KeSetLastUserError(res);
        return res;
    }

    struct dc* dc;
    struct brush* brush;
    res = CdAcquirePaintObjects((void*) a, args.brush, args.flags, &dc, &brush);
    if (res != 0) {
        KeSetLastUserError(res);
        return res;
    }

    if (brush != NULL) {
        res = CdPaintRoundedRectWithBrush(dc, args.x, args.y, args.w, args.h, args.radius, brush);
    } else {
        res = CdPaintRoundedRect(dc, args.x, args.y, args.w, args.h, args.radius);
    }

    CdReleasePaintObjects(dc, brush);
    if (res != 0) {
        KeSetLastUserError(res);
    }
    return res;
}

static pageableuserexec int PaintRoundedRectInternal(dc_t dc, int x, int y, int width, int height,
    int radius, brush_t brush, int flags) {
    struct user_shape_args args = {0};
    args.x = x;
    args.y = y;
    args.w = width;
    args.h = height;
    args.radius = radius;
    args.brush = brush;
    args.flags = flags;
    return (int) SystemCall3(SYS_PaintRoundedRect, (size_t) dc, (size_t) &args);
}

export pageableuserexec int PaintRoundedRectWithBrush(dc_t dc, int x, int y, int width, int height,
    int radius, brush_t brush) {
    return PaintRoundedRectInternal(dc, x, y, width, height, radius, brush, 0);
}

export pageableuserexec int PaintRoundedRect(dc_t dc, int x, int y, int width, int height,
    int radius) {
    return PaintRoundedRectInternal(dc, x, y, width, height, radius, NULL, PAINT_USE_DC_BRUSH);
}
