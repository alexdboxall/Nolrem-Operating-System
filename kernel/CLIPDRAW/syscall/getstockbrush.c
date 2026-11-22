#include <common.h>
#include <obj.h>
#include <string.h>
#include <syscall.h>
#include "../api.h"
#include "../clipdraw_internal.h"

/* Stock brushes are shared by everyone, are read-only (see CdIsStockBrush),
   and ignore user Ref/Deref, so we hand out the pointer without a new reference. */
pageable size_t SysGetStockBrush(size_t type, size_t, size_t, size_t) {
    struct brush* br = CdGetStockBrush((int) type);
    if (br == NULL) {
        KeSetLastUserError(EINVAL);
        return 0;
    }
    return (size_t) br;
}

export pageableuserexec brush_t GetStockBrush(int type) {
    return (brush_t) SystemCall2(SYS_GetStockBrush, type);
}
