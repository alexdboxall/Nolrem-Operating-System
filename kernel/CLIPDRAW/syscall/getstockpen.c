#include <common.h>
#include <obj.h>
#include <string.h>
#include <syscall.h>
#include "../api.h"
#include "../clipdraw_internal.h"

/* Stock pens are shared by everyone, are read-only (see CdIsStockPen), and
   their handles are permanent and ignore user Ref/Deref, so we hand out the
   same handle every time, without making a new one. */
pageable size_t SysGetStockPen(size_t type, size_t, size_t, size_t) {
    struct user_obj_ref* handle = CdGetStockPenHandle((int) type);
    if (handle == NULL) {
        KeSetLastUserError(EINVAL);
        return 0;
    }
    return (size_t) handle;
}

export pageableuserexec pen_t GetStockPen(int type) {
    return (pen_t) SystemCall2(SYS_GetStockPen, type);
}
