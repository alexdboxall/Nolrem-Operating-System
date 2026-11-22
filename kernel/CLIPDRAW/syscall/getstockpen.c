#include <common.h>
#include <obj.h>
#include <string.h>
#include <syscall.h>
#include "../api.h"
#include "../clipdraw_internal.h"

/* Stock pens are shared by everyone, are read-only (see CdIsStockPen), and
   ignore user Ref/Deref, so we hand out the pointer without a new reference. */
pageable size_t SysGetStockPen(size_t type, size_t, size_t, size_t) {
    struct pen* pen = CdGetStockPen((int) type);
    if (pen == NULL) {
        KeSetLastUserError(EINVAL);
        return 0;
    }
    return (size_t) pen;
}

export pageableuserexec pen_t GetStockPen(int type) {
    return (pen_t) SystemCall2(SYS_GetStockPen, type);
}
