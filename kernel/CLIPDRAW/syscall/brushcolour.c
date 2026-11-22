#include <common.h>
#include <obj.h>
#include <string.h>
#include <syscall.h>
#include "../api.h"
#include "../clipdraw_internal.h"

#define OP_GET_PRIMARY      0
#define OP_SET_PRIMARY      1
#define OP_GET_SECONDARY    2
#define OP_SET_SECONDARY    3

static bool CdIsGetOp(size_t op) {
    return op == OP_GET_PRIMARY || op == OP_GET_SECONDARY;
}

/*
 * GET returns the colour. Every 32-bit value is a valid colour, so on error it
 * returns 0 (transparent) and the only sign of failure is the last error.
 * SET returns 0 on success, or an errno.
 */
pageable size_t SysBrushColour(size_t a, size_t op, size_t argb, size_t) {
    struct brush* br = (struct brush*) a;

    if (op > OP_SET_SECONDARY) {
        KeSetLastUserError(EINVAL);
        return EINVAL;
    }
    if (!KeValidateUserObjectAndAtomicallyRef(br, UOBJ_BRUSH)) {
        KeSetLastUserError(EINVAL);
        return CdIsGetOp(op) ? 0 : EINVAL;
    }
    if (!CdIsGetOp(op) && CdIsStockBrush(br)) {
        KeDerefObject(br);
        KeSetLastUserError(EACCES);
        return EACCES;
    }

    size_t retv = 0;
    switch (op) {
    case OP_GET_PRIMARY:
        retv = CdGetBrushColour(br);
        break;
    case OP_SET_PRIMARY:
        retv = CdSetBrushColour(br, (colour_t) argb);
        break;
    case OP_GET_SECONDARY:
        retv = CdGetBrushSecondaryColour(br);
        break;
    case OP_SET_SECONDARY:
        retv = CdSetBrushSecondaryColour(br, (colour_t) argb);
        break;
    }
    KeDerefObject(br);
    return retv;
}

export pageableuserexec colour_t GetBrushColour(brush_t br) {
    return (colour_t) SystemCall3(SYS_BrushColour, (size_t) br, OP_GET_PRIMARY);
}

export pageableuserexec int SetBrushColour(brush_t br, colour_t argb) {
    return (int) SystemCall4(SYS_BrushColour, (size_t) br, OP_SET_PRIMARY, argb);
}

export pageableuserexec colour_t GetBrushSecondaryColour(brush_t br) {
    return (colour_t) SystemCall3(SYS_BrushColour, (size_t) br, OP_GET_SECONDARY);
}

export pageableuserexec int SetBrushSecondaryColour(brush_t br, colour_t argb) {
    return (int) SystemCall4(SYS_BrushColour, (size_t) br, OP_SET_SECONDARY, argb);
}
