#include <common.h>
#include <obj.h>
#include <string.h>
#include <syscall.h>
#include "../api.h"
#include "../clipdraw_internal.h"

pageable size_t SysIsRegionEqual(size_t a, size_t b, size_t, size_t) {
    struct uregion* ua = (struct uregion*) a;
    struct uregion* ub = (struct uregion*) b;

    if (KeIsOutOfMemory()) {
        KeSetLastUserError(ENOMEM);
        return 0;
    }
    if (!KeValidateUserObjectAndAtomicallyRef(ua, UOBJ_REGION)) {
        KeSetLastUserError(EINVAL);
        return 0;
    }
    if (!KeValidateUserObjectAndAtomicallyRef(ub, UOBJ_REGION)) {
        KeDerefObject(ua);
        KeSetLastUserError(EINVAL);
        return 0;
    }

    bool equal = CdIsRegionEqual(ua->rgn, ub->rgn);
    
    KeDerefObject(ua);
    KeDerefObject(ub);
    return equal;
}

export pageableuserexec bool IsRegionEqual(region_t a, region_t b) {
    return (region_t) SystemCall3(SYS_IsRegionEqual, (size_t) a, (size_t) b);
}