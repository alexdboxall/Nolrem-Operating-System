#include <common.h>
#include <obj.h>
#include <string.h>
#include <syscall.h>
#include "../api.h"
#include "../clipdraw_internal.h"

pageable size_t SysIsRegionEqual(size_t a, size_t b, size_t, size_t) {
    struct uregion* ua = (struct uregion*) a;
    struct uregion* ub = (struct uregion*) b;

    if (!ValidateUserObjectAndAtomicallyRef(ua, UOBJ_REGION)) {
        return 0;
    }
    if (!ValidateUserObjectAndAtomicallyRef(ub, UOBJ_REGION)) {
        DerefObject(ua);
        return 0;
    }

    bool equal = CdIsRegionEqual(ua->rgn, ub->rgn);
    
    DerefObject(ua);
    DerefObject(ub);
    return equal;
}

export pageableuserexec bool IsRegionEqual(region_t a, region_t b) {
    return (region_t) SystemCall(SYS_IsRegionEqual, (size_t) a, (size_t) b, 0, 0);
}