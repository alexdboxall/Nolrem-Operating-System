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
pageable size_t SysPaintEllipse(size_t a, size_t args_ptr, size_t, size_t) {
    if (IsOutOfMemory()) {
        KeSetLastUserError(ENOMEM);
        return ENOMEM;
    }

    struct user_shape_args args;
    int res = CdReadShapeArgs(args_ptr, &args);
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
        res = CdPaintEllipseWithBrush(dc, args.x, args.y, args.w, args.h, brush);
    } else {
        res = CdPaintEllipse(dc, args.x, args.y, args.w, args.h);
    }

    CdReleasePaintObjects(dc, brush);
    if (res != 0) {
        KeSetLastUserError(res);
    }
    return res;
}

static pageableuserexec int PaintEllipseInternal(dc_t dc, int x, int y, int width, int height,
    brush_t brush, int flags) {
    struct user_shape_args args = {0};
    args.x = x;
    args.y = y;
    args.w = width;
    args.h = height;
    args.brush = brush;
    args.flags = flags;
    return (int) SystemCall3(SYS_PaintEllipse, (size_t) dc, (size_t) &args);
}

export pageableuserexec int PaintEllipseWithBrush(dc_t dc, int x, int y, int width, int height,
    brush_t brush) {
    return PaintEllipseInternal(dc, x, y, width, height, brush, 0);
}

export pageableuserexec int PaintEllipse(dc_t dc, int x, int y, int width, int height) {
    return PaintEllipseInternal(dc, x, y, width, height, NULL, PAINT_USE_DC_BRUSH);
}
