#include <common.h>
#include <obj.h>
#include <string.h>
#include <syscall.h>
#include "../api.h"
#include "../clipdraw_internal.h"

pageable size_t SysTranslateRegion(size_t a, size_t _x, size_t _y, size_t) {
    struct uregion* ua = (struct uregion*) a;

    if (!KeValidateUserObjectAndAtomicallyRef(ua, UOBJ_REGION)) {
        KeSetLastUserError(EINVAL);
        return 0;
    }

    int x = (int) _x;
    int y = (int) _y;

    int retv = CdTranslateRegion(&ua->rgn, x, y);
    KeDerefObject(ua);
    return retv;
}

export pageableuserexec int TranslateRegion(region_t a, int offx, int offy) {
    return SystemCall4(SYS_TranslateRegion, (size_t) a, offx, offy);
}