#include <common.h>
#include <obj.h>
#include <string.h>
#include <syscall.h>
#include <uobj.h>

#define TYPE_REF    0
#define TYPE_DEREF  1

pageable size_t SysRef(size_t a, size_t type, size_t, size_t) {
    void* uo = (void*) a;
    if (!KeValidateUserObjectAndAtomicallyRef(uo, UOBJ_ANYTYPE)) {
        KeSetLastUserError(EINVAL);
        return 0;
    }

    /* Objects marked permanent (e.g. the stock brushes) ignore this: KeUserRef
       and KeUserDeref do nothing to them, so a user Deref can't free them. */
    int retv;
    if (type == TYPE_REF) {
        retv = KeUserRef(uo);
    } else if (type == TYPE_DEREF) {
        retv = KeUserDeref(uo);
    } else {
        retv = EINVAL;
    }
    KeDerefObject(uo);
    return retv;
}

export pageableuserexec void Ref(any_t obj) {
    SystemCall3(SYS_Ref, (size_t) obj, TYPE_REF);
}

export pageableuserexec void Deref(any_t obj) {
    SystemCall3(SYS_Ref, (size_t) obj, TYPE_DEREF);
}
