#include <common.h>
#include <obj.h>
#include <string.h>
#include <syscall.h>
#include "../api.h"
#include "../clipdraw_internal.h"

pageable size_t SysIsRegionEqual(size_t a, size_t b, size_t, size_t) {
    if (KeIsOutOfMemory()) {
        KeSetLastUserError(ENOMEM);
        return 0;
    }
    struct uregion* ua = KeValidateUserObjectAndAtomicallyRef((void*) a, OBJ_REGION);
    if (ua == NULL) {
        KeSetLastUserError(EINVAL);
        return 0;
    }
    struct uregion* ub = KeValidateUserObjectAndAtomicallyRef((void*) b, OBJ_REGION);
    if (ub == NULL) {
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