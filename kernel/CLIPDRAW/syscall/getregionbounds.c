#include <common.h>
#include <obj.h>
#include <string.h>
#include <transfer.h>
#include <syscall.h>
#include "../api.h"
#include "../clipdraw_internal.h"

pageable size_t SysGetRegionBounds(size_t rect_ptr, size_t rgn, size_t, size_t) {
    struct uregion* ua = (struct uregion*) rgn;
    if (!ValidateUserObjectAndAtomicallyRef(ua, UOBJ_REGION)) {
        return 0;
    }
    struct rect rect = CdGetRegionBounds(ua->rgn);
    DerefObject(ua);

    struct transfer tr = CreateTransferWritingToUser((void*) rect_ptr, sizeof(struct rect), 0);
    return PerformTransfer(&rect, &tr, sizeof(struct rect));
}

export pageableuserexec struct rect GetRegionBounds(region_t rgn) {
    struct rect r;
    size_t res = SystemCall(SYS_GetRegionBounds, (size_t) &r, (size_t) rgn, 0, 0);
    if (res == 0) {
        return r;
    } else {
        struct rect r2 = {0};
        return r2; 
    }
}
