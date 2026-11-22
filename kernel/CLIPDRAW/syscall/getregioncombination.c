#include <common.h>
#include <obj.h>
#include <string.h>
#include <syscall.h>
#include "../api.h"
#include "../clipdraw_internal.h"

pageable size_t SysGetRegionCombination(size_t a, size_t b, size_t mode, size_t) {
    if (KeIsOutOfMemory()) {
        KeSetLastUserError(ENOMEM);
        return 0;
    }
    if    (mode != REGION_COMBINE_DIFFERENCE 
        && mode != REGION_COMBINE_INTERSECT
        && mode != REGION_COMBINE_UNION
        && mode != REGION_COMBINE_XOR
    ) {
        KeSetLastUserError(EINVAL);
        return 0;
    }

    struct uregion* ua = (struct uregion*) a;
    struct uregion* ub = (struct uregion*) b;

    if (!KeValidateUserObjectAndAtomicallyRef(ua, UOBJ_REGION)) {
        KeSetLastUserError(EINVAL);
        return 0;
    }
    if (!KeValidateUserObjectAndAtomicallyRef(ub, UOBJ_REGION)) {
        KeSetLastUserError(EINVAL);
        KeDerefObject(ua);
        return 0;
    }

    struct region c = CdGetRegionCombination(
        mode, UserRegionToRegion(ua), UserRegionToRegion(ub)
    );
    
    KeDerefObject(ua);
    KeDerefObject(ub);
    return (size_t) RegionToUserRegion(c);
}

export pageableuserexec region_t GetRegionCombination(int mode, region_t a, region_t b) {
    return (region_t) SystemCall4(SYS_GetRegionCombination, (size_t) a, (size_t) b, mode);
}

export pageableuserexec void GetRegionCombinationInPlace(int mode, region_t* a, region_t b) {
    region_t c = GetRegionCombination(mode, *a, b);
    Deref(*a);
    *a = c;
}
