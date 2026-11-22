#include <common.h>
#include <obj.h>
#include <string.h>
#include <syscall.h>
#include "../api.h"
#include "../clipdraw_internal.h"

#define OP_GET  0
#define OP_SET  1

/*
 * GET returns the colour. Every 32-bit value is a valid colour, so on error it
 * returns 0 (transparent) and the only sign of failure is the last error.
 * SET returns 0 on success, or an errno.
 */
pageable size_t SysPenColour(size_t a, size_t op, size_t argb, size_t) {
    struct pen* pen = (struct pen*) a;

    if (op != OP_GET && op != OP_SET) {
        KeSetLastUserError(EINVAL);
        return EINVAL;
    }
    if (!KeValidateUserObjectAndAtomicallyRef(pen, UOBJ_PEN)) {
        KeSetLastUserError(EINVAL);
        return op == OP_GET ? 0 : EINVAL;
    }

    size_t retv;
    if (op == OP_GET) {
        retv = CdGetPenColour(pen);
    } else if (CdIsStockPen(pen)) {
        KeSetLastUserError(EACCES);
        retv = EACCES;
    } else {
        retv = (size_t) CdSetPenColour(pen, (colour_t) argb);
    }
    KeDerefObject(pen);
    return retv;
}

export pageableuserexec colour_t GetPenColour(pen_t pen) {
    return (colour_t) SystemCall3(SYS_PenColour, (size_t) pen, OP_GET);
}

export pageableuserexec int SetPenColour(pen_t pen, colour_t col) {
    return (int) SystemCall4(SYS_PenColour, (size_t) pen, OP_SET, col);
}
