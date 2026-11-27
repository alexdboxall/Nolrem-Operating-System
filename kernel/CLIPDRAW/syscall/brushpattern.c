#include <common.h>
#include <obj.h>
#include <string.h>
#include <syscall.h>
#include "../api.h"
#include "../clipdraw_internal.h"

#define OP_GET  0
#define OP_SET  1

/*
 * GET returns the pattern, or BRUSH_INVALID on error (with the last error set).
 * SET returns 0 on success, or an errno.
 */
pageable size_t SysBrushPattern(size_t a, size_t op, size_t pattern, size_t) {
    if (op != OP_GET && op != OP_SET) {
        KeSetLastUserError(EINVAL);
        return EINVAL;
    }
    struct brush* br = KeValidateUserObjectAndAtomicallyRef((void*) a, OBJ_BRUSH);
    if (br == NULL) {
        KeSetLastUserError(EINVAL);
        return op == OP_GET ? (size_t) BRUSH_INVALID : EINVAL;
    }

    size_t retv;
    if (op == OP_GET) {
        retv = (size_t) CdGetBrushPattern(br);
    } else if (CdIsStockBrush(br)) {
        retv = EACCES;
    } else {
        retv = (size_t) CdSetBrushPattern(br, (int) pattern);
    }
    KeDerefObject(br);
    return retv;
}

export pageableuserexec int GetBrushPattern(brush_t br) {
    return (int) SystemCall3(SYS_BrushPattern, (size_t) br, OP_GET);
}

export pageableuserexec int SetBrushPattern(brush_t br, int pattern) {
    return (int) SystemCall4(SYS_BrushPattern, (size_t) br, OP_SET, pattern);
}
