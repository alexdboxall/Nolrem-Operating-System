#include <common.h>
#include <obj.h>
#include <string.h>
#include <syscall.h>
#include "../api.h"
#include "../clipdraw_internal.h"

/*
 * a = DC, rgn_ptr = region.
 * Returns 0 on success, or an errno. Drawing is clipped to the DC's clip region.
 */
pageable size_t SysInvertRegion(size_t a, size_t rgn_ptr, size_t, size_t) {
    struct dc* dc = (struct dc*) a;
    struct uregion* urgn = (struct uregion*) rgn_ptr;

    if (IsOutOfMemory()) {
        KeSetLastUserError(ENOMEM);
        return ENOMEM;
    }
    if (!ValidateUserObjectAndAtomicallyRef(dc, UOBJ_DC)) {
        KeSetLastUserError(EINVAL);
        return EINVAL;
    }
    if (!ValidateUserObjectAndAtomicallyRef(urgn, UOBJ_REGION)) {
        DerefObject(dc);
        KeSetLastUserError(EINVAL);
        return EINVAL;
    }

    int res = CdInvertRegion(dc, UserRegionToRegion(urgn));

    DerefObject(urgn);
    DerefObject(dc);
    if (res != 0) {
        KeSetLastUserError(res);
    }
    return res;
}

export pageableuserexec int InvertRegion(dc_t dc, region_t rgn) {
    return (int) SystemCall3(SYS_InvertRegion, (size_t) dc, (size_t) rgn);
}
