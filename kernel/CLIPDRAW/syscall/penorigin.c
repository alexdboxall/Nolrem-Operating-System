#include <common.h>
#include <obj.h>
#include <string.h>
#include <transfer.h>
#include <syscall.h>
#include <usersys.h>
#include "../api.h"
#include "../clipdraw_internal.h"

#define OP_GET  0
#define OP_SET  1

/*
 * SET: a = x, b = y.
 * GET: a = pointer to a struct point to write the origin to.
 * Both return 0 on success, or an errno.
 */
pageable size_t SysPenOrigin(size_t pen_ptr, size_t op, size_t a, size_t b) {
    if (op != OP_GET && op != OP_SET) {
        KeSetLastUserError(EINVAL);
        return EINVAL;
    }
    struct pen* pen = KeValidateUserObjectAndAtomicallyRef((void*) pen_ptr, OBJ_PEN);
    if (pen == NULL) {
        KeSetLastUserError(EINVAL);
        return EINVAL;
    }

    if (op == OP_SET) {
        int res = CdIsStockPen(pen) ? EACCES : CdSetPenOrigin(pen, (int) a, (int) b);
        KeDerefObject(pen);
        return res;
    }

    struct point origin = CdGetPenOrigin(pen);
    KeDerefObject(pen);

    struct transfer tr = KeCreateTransferWritingToUser((void*) a, sizeof(struct point), 0);
    return KePerformTransfer(&origin, &tr, sizeof(struct point));
}

export pageableuserexec struct point GetPenOrigin(pen_t pen) {
    struct point p = {0};
    size_t res = SystemCall4(SYS_PenOrigin, (size_t) pen, OP_GET, (size_t) &p);
    if (res != 0) {
        SetLastError((int) res);
        struct point zero = {0};
        return zero;
    }
    return p;
}

export pageableuserexec int SetPenOrigin(pen_t pen, int x, int y) {
    return (int) SystemCall(SYS_PenOrigin, (size_t) pen, OP_SET, x, y);
}
