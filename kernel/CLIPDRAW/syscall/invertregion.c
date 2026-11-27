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
    if (KeIsOutOfMemory()) {
        KeSetLastUserError(ENOMEM);
        return ENOMEM;
    }
    struct dc* dc = KeValidateUserObjectAndAtomicallyRef((void*) a, OBJ_DC);
    if (dc == NULL) {
        KeSetLastUserError(EINVAL);
        return EINVAL;
    }
    struct uregion* urgn = KeValidateUserObjectAndAtomicallyRef((void*) rgn_ptr, OBJ_REGION);
    if (urgn == NULL) {
        KeDerefObject(dc);
        KeSetLastUserError(EINVAL);
        return EINVAL;
    }

    int res = CdInvertRegion(dc, CdUserRegionToRegion(urgn));

    KeDerefObject(urgn);
    KeDerefObject(dc);
    if (res != 0) {
        KeSetLastUserError(res);
    }
    return res;
}

export pageableuserexec int InvertRegion(dc_t dc, region_t rgn) {
    return (int) SystemCall3(SYS_InvertRegion, (size_t) dc, (size_t) rgn);
}
