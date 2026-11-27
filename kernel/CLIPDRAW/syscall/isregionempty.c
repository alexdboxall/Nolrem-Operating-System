#include <common.h>
#include <obj.h>
#include <string.h>
#include <syscall.h>
#include "../api.h"
#include "../clipdraw_internal.h"

pageable size_t SysIsRegionEmpty(size_t a, size_t, size_t, size_t) {
    struct uregion* ua = KeValidateUserObjectAndAtomicallyRef((void*) a, OBJ_REGION);
    if (ua == NULL) {
        KeSetLastUserError(EINVAL);
        return 0;
    }

    bool empty = CdIsRegionEmpty(ua->rgn);
    KeDerefObject(ua);
    return empty;
}

export pageableuserexec bool IsRegionEmpty(region_t a) {
    return (region_t) SystemCall2(SYS_IsRegionEmpty, (size_t) a);
}