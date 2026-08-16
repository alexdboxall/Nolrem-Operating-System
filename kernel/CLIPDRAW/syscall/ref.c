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
        return 0;
    }
    if (type == TYPE_REF) {
        UserRef(uo);
    } else if (type == TYPE_DEREF) {
        UserDeref(uo);
    }
    DerefObject(uo);
    return 0;
}

export pageableuserexec void Ref(any_t obj) {
    SystemCall(SYS_Ref, (size_t) obj, TYPE_REF, 0, 0);
}

export pageableuserexec void Deref(any_t obj) {
    SystemCall(SYS_Ref, (size_t) obj, TYPE_DEREF, 0, 0);
}
