#include <common.h>
#include <obj.h>
#include <string.h>
#include <syscall.h>
#include "../api.h"
#include "../clipdraw_internal.h"

#define TYPE_REF    0
#define TYPE_DEREF  1

pageable size_t SysRef(size_t a, size_t type, size_t, size_t) {
    void* uo = (void*) a;
    if (!ValidateUserObjectAndAtomicallyRef(uo, UOBJ_ANYTYPE)) {
        KeSetLastUserError(EINVAL);
        return 0;
    }

    /* Stock brushes/pens are shared and permanent. A user Deref must never be
       able to free them, so Ref/Deref on them is a no-op. */
    struct user_obj_header* hdr = uo;
    if ((hdr->user_type == UOBJ_BRUSH && CdIsStockBrush(uo))
        || (hdr->user_type == UOBJ_PEN && CdIsStockPen(uo))) {
        DerefObject(uo);
        return 0;
    }

    int retv;
    if (type == TYPE_REF) {
        retv = UserRef(uo);
    } else if (type == TYPE_DEREF) {
        retv = UserDeref(uo);
    } else {
        retv = EINVAL;
    }
    DerefObject(uo);
    return retv;
}

export pageableuserexec void Ref(any_t obj) {
    SystemCall3(SYS_Ref, (size_t) obj, TYPE_REF);
}

export pageableuserexec void Deref(any_t obj) {
    SystemCall3(SYS_Ref, (size_t) obj, TYPE_DEREF);
}
