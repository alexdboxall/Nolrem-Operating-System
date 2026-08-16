#include <common.h>
#include <obj.h>
#include <string.h>
#include <syscall.h>
#include "../api.h"
#include "../clipdraw_internal.h"

pageable size_t SysIsPointInRegion(size_t a, size_t x, size_t y, size_t) {
    struct uregion* ua = (struct uregion*) a;

    if (!ValidateUserObjectAndAtomicallyRef(ua, UOBJ_REGION)) {
        return 0;
    }

    bool in = CdIsPointInRegion(ua->rgn, x, y);
    DerefObject(ua);
    return in;
}

export pageableuserexec bool IsPointInRegion(region_t a, int x, int y) {
    return (region_t) SystemCall(SYS_IsPointInRegion, (size_t) a, x, y, 0);
}