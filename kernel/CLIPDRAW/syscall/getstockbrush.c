#include <common.h>
#include <obj.h>
#include <string.h>
#include <syscall.h>
#include "../api.h"
#include "../clipdraw_internal.h"

/* Stock brushes are shared by everyone, are read-only (see CdIsStockBrush), and
   their handles are permanent and ignore user Ref/Deref, so we hand out the
   same handle every time, without making a new one. */
pageable size_t SysGetStockBrush(size_t type, size_t, size_t, size_t) {
    struct user_obj_ref* handle = CdGetStockBrushHandle((int) type);
    if (handle == NULL) {
        KeSetLastUserError(EINVAL);
        return 0;
    }
    return (size_t) handle;
}

export pageableuserexec brush_t GetStockBrush(int type) {
    return (brush_t) SystemCall2(SYS_GetStockBrush, type);
}
