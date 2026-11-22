#include <common.h>
#include <obj.h>
#include <string.h>
#include <syscall.h>
#include "../api.h"
#include "../clipdraw_internal.h"

pageable size_t SysCopyRegion(size_t a, size_t, size_t, size_t) {
    struct uregion* ua = (struct uregion*) a;

    if (KeIsOutOfMemory()) {
        KeSetLastUserError(ENOMEM);
        return 0;
    }
    if (!KeValidateUserObjectAndAtomicallyRef(ua, UOBJ_REGION)) {
        KeSetLastUserError(EINVAL);
        return 0;
    }

    struct region rgn2 = CdCopyRegion(ua->rgn);
    size_t retv = (size_t) RegionToUserRegion(rgn2);
    KeDerefObject(ua);
    return retv;
}

export pageableuserexec region_t CopyRegion(region_t a) {
    return (region_t) SystemCall2(SYS_CopyRegion, (size_t) a);
}