#include <common.h>
#include <obj.h>
#include <string.h>
#include <syscall.h>
#include <uobj.h>

#define TYPE_REF    0
#define TYPE_DEREF  1

pageable size_t SysRef(size_t handle, size_t type, size_t, size_t) {
    /* This is the one call that works on the handle rather than the object under it. We hold a reference to
       the handle's wrapper, which the last Deref() would otherwise be taking away from under us. */
    struct user_obj_ref* ref = KeValidateUserRefAndAtomicallyRef((void*) handle);
    if (ref == NULL) {
        KeSetLastUserError(EINVAL);
        return 0;
    }

    /* Handles marked permanent (e.g. the stock brushes) ignore this: KeUserRef
       and KeUserDeref do nothing to them, so a user Deref can't free them. */
    int retv;
    if (type == TYPE_REF) {
        retv = KeUserRef(ref);
    } else if (type == TYPE_DEREF) {
        retv = KeUserDeref(ref);
    } else {
        retv = EINVAL;
    }
    KeDerefObject(ref);
    return retv;
}

export pageableuserexec void Ref(any_t obj) {
    SystemCall3(SYS_Ref, (size_t) obj, TYPE_REF);
}

export pageableuserexec void Deref(any_t obj) {
    SystemCall3(SYS_Ref, (size_t) obj, TYPE_DEREF);
}
