#include <common.h>
#include <obj.h>
#include <string.h>
#include <syscall.h>
#include "../api.h"
#include "../clipdraw_internal.h"

#define OP_GET  0
#define OP_SET  1

/*
 * GET returns the pattern. Custom pens have no standard pattern, so they return
 * PEN_INVALID with no error set. Any other failure also returns PEN_INVALID,
 * but with the last error set.
 * SET returns 0 on success, or an errno.
 */
pageable size_t SysPenPattern(size_t a, size_t op, size_t pattern, size_t) {
    struct pen* pen = (struct pen*) a;

    if (op != OP_GET && op != OP_SET) {
        KeSetLastUserError(EINVAL);
        return EINVAL;
    }
    if (!ValidateUserObjectAndAtomicallyRef(pen, UOBJ_PEN)) {
        KeSetLastUserError(EINVAL);
        return op == OP_GET ? (size_t) PEN_INVALID : EINVAL;
    }

    size_t retv;
    if (op == OP_GET) {
        retv = (size_t) CdGetPenPattern(pen);
    } else if (CdIsStockPen(pen)) {
        KeSetLastUserError(EACCES);
        retv = EACCES;
    } else if (pattern > _HIGHEST_USED_PEN_TYPE) {
        KeSetLastUserError(EINVAL);
        retv = EINVAL;
    } else {
        retv = (size_t) CdSetPenPattern(pen, (int) pattern);
    }
    DerefObject(pen);
    return retv;
}

export pageableuserexec int GetPenPattern(pen_t pen) {
    return (int) SystemCall3(SYS_PenPattern, (size_t) pen, OP_GET);
}

export pageableuserexec int SetPenPattern(pen_t pen, int pattern) {
    return (int) SystemCall4(SYS_PenPattern, (size_t) pen, OP_SET, pattern);
}
