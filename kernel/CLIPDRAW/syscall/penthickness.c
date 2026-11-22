#include <common.h>
#include <obj.h>
#include <string.h>
#include <syscall.h>
#include "../api.h"
#include "../clipdraw_internal.h"

#define OP_GET  0
#define OP_SET  1

/*
 * GET returns the thickness, which is always at least 1, so 0 means an error
 * (with the last error set).
 * SET returns 0 on success, or an errno.
 */
pageable size_t SysPenThickness(size_t a, size_t op, size_t thickness, size_t) {
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
        retv = (size_t) CdGetPenThickness(pen);
    } else if (CdIsStockPen(pen)) {
        KeSetLastUserError(EACCES);
        retv = EACCES;
    } else if (thickness < 1 || thickness > PEN_MAX_THICKNESS) {
        KeSetLastUserError(EINVAL);
        retv = EINVAL;
    } else {
        retv = (size_t) CdSetPenThickness(pen, (int) thickness);
    }
    KeDerefObject(pen);
    return retv;
}

export pageableuserexec int GetPenThickness(pen_t pen) {
    return (int) SystemCall3(SYS_PenThickness, (size_t) pen, OP_GET);
}

export pageableuserexec int SetPenThickness(pen_t pen, int thickness) {
    return (int) SystemCall4(SYS_PenThickness, (size_t) pen, OP_SET, thickness);
}
