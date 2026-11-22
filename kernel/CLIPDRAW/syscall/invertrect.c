#include <common.h>
#include <obj.h>
#include <string.h>
#include <syscall.h>
#include "../api.h"
#include "../clipdraw_internal.h"

/*
 * a = DC, b = pointer to a struct user_shape_args (only x, y, w and h are used).
 * Returns 0 on success, or an errno. Drawing is clipped to the DC's clip region.
 */
pageable size_t SysInvertRect(size_t a, size_t args_ptr, size_t, size_t) {
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

    struct dc* dc = (struct dc*) a;
    if (!ValidateUserObjectAndAtomicallyRef(dc, UOBJ_DC)) {
        KeSetLastUserError(EINVAL);
        return EINVAL;
    }

    res = CdInvertRect(dc, args.x, args.y, args.w, args.h);

    DerefObject(dc);
    if (res != 0) {
        KeSetLastUserError(res);
    }
    return res;
}

export pageableuserexec int InvertRect(dc_t dc, int x, int y, int width, int height) {
    /* The flags are checked, so give it the same ones as the paint calls. */
    struct user_shape_args args = {0};
    args.x = x;
    args.y = y;
    args.w = width;
    args.h = height;
    args.flags = PAINT_USE_DC_BRUSH;
    return (int) SystemCall3(SYS_InvertRect, (size_t) dc, (size_t) &args);
}
