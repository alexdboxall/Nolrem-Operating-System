#include <common.h>
#include <obj.h>
#include <string.h>
#include <syscall.h>
#include "../api.h"
#include "../clipdraw_internal.h"

pageable size_t SysIsRegionEmpty(size_t a, size_t, size_t, size_t) {
    struct uregion* ua = (struct uregion*) a;

    if (!ValidateUserObjectAndAtomicallyRef(ua, UOBJ_REGION)) {
        return 0;
    }

    bool empty = CdIsRegionEmpty(ua->rgn);
    DerefObject(ua);
    return empty;
}

export pageableuserexec bool IsRegionEmpty(region_t a) {
    return (region_t) SystemCall(SYS_IsRegionEmpty, (size_t) a, 0, 0, 0);
}