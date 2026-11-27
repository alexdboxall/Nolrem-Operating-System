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
pageable size_t SysBrushOrigin(size_t br_ptr, size_t op, size_t a, size_t b) {
    if (op != OP_GET && op != OP_SET) {
        KeSetLastUserError(EINVAL);
        return EINVAL;
    }
    struct brush* br = KeValidateUserObjectAndAtomicallyRef((void*) br_ptr, OBJ_BRUSH);
    if (br == NULL) {
        KeSetLastUserError(EINVAL);
        return EINVAL;
    }

    if (op == OP_SET) {
        int res = CdIsStockBrush(br) ? EACCES : CdSetBrushOrigin(br, (int) a, (int) b);
        KeDerefObject(br);
        return res;
    }

    struct point origin = CdGetBrushOrigin(br);
    KeDerefObject(br);

    struct transfer tr = KeCreateTransferWritingToUser((void*) a, sizeof(struct point), 0);
    return KePerformTransfer(&origin, &tr, sizeof(struct point));
}

export pageableuserexec struct point GetBrushOrigin(brush_t br) {
    struct point p = {0};
    size_t res = SystemCall4(SYS_BrushOrigin, (size_t) br, OP_GET, (size_t) &p);
    if (res != 0) {
        SetLastError((int) res);
        struct point zero = {0};
        return zero;
    }
    return p;
}

export pageableuserexec int SetBrushOrigin(brush_t br, int x, int y) {
    return (int) SystemCall(SYS_BrushOrigin, (size_t) br, OP_SET, x, y);
}
