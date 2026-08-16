#include <common.h>
#include <obj.h>
#include <string.h>
#include <syscall.h>
#include "../api.h"
#include "../clipdraw_internal.h"

pageable size_t SysGetRegionCombination(size_t a, size_t b, size_t mode, size_t) {
    if    (mode != REGION_COMBINE_DIFFERENCE 
        && mode != REGION_COMBINE_INTERSECT
        && mode != REGION_COMBINE_UNION
        && mode != REGION_COMBINE_XOR
    ) {
        return 0;
    }

    struct uregion* ua = (struct uregion*) a;
    struct uregion* ub = (struct uregion*) b;

    if (!ValidateUserRegionAndAtomicallyRef(ua)) {
        return 0;
    }
    if (!ValidateUserRegionAndAtomicallyRef(ub)) {
        DerefObject(ua);
        return 0;
    }

    struct region c = CdGetRegionCombination(
        mode, UserRegionToRegion(ua), UserRegionToRegion(ub)
    );
    
    DerefObject(ua);
    DerefObject(ub);
    return (size_t) RegionToUserRegion(c);
}

export pageableuserexec region_t GetRegionCombination(int mode, region_t a, region_t b) {
    return (region_t) SystemCall(SYS_CreateRectRegion, (size_t) a, (size_t) b, mode, 0);
}

export pageableuserexec void GetRegionCombinationInPlace(int mode, region_t* a, region_t b) {
    region_t c = GetRegionCombination(mode, *a, b);
    Deref(*a);
    *a = c;
}
