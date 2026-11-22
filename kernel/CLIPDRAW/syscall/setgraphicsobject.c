#include <common.h>
#include <obj.h>
#include <string.h>
#include <syscall.h>
#include "../api.h"
#include "../clipdraw_internal.h"

/*
 * Returns 0 on success, or an errno. Only brushes and pens can be selected into
 * a DC; CdSetGraphicsObject rejects any other type with EINVAL.
 *
 * The DC takes its own reference to `obj`, so the caller is free to Deref() it
 * straight after this returns.
 */
pageable size_t SysSetGraphicsObject(size_t dc_ptr, size_t obj_ptr, size_t, size_t) {
    struct dc* dc = (struct dc*) dc_ptr;
    void* obj = (void*) obj_ptr;

    if (!ValidateUserObjectAndAtomicallyRef(dc, UOBJ_DC)) {
        KeSetLastUserError(EINVAL);
        return EINVAL;
    }
    if (!ValidateUserObjectAndAtomicallyRef(obj, UOBJ_ANYTYPE)) {
        DerefObject(dc);
        KeSetLastUserError(EINVAL);
        return EINVAL;
    }

    int res = CdSetGraphicsObject(dc, obj);

    DerefObject(obj);
    DerefObject(dc);

    if (res != 0) {
        KeSetLastUserError(res);
    }
    return res;
}

export pageableuserexec int SetGraphicsObject(dc_t dc, void* obj) {
    return (int) SystemCall3(SYS_SetGraphicsObject, (size_t) dc, (size_t) obj);
}
