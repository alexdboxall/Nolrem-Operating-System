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
 * The DC takes its own reference to the object (not to the handle), so the caller
 * is free to Deref() the handle straight after this returns.
 */
pageable size_t SysSetGraphicsObject(size_t dc_handle, size_t obj_handle, size_t, size_t) {
    struct dc* dc = KeValidateUserObjectAndAtomicallyRef((void*) dc_handle, OBJ_DC);
    if (dc == NULL) {
        KeSetLastUserError(EINVAL);
        return EINVAL;
    }
    void* obj = KeValidateUserObjectAndAtomicallyRef((void*) obj_handle, OBJ_ANYTYPE);
    if (obj == NULL) {
        KeDerefObject(dc);
        KeSetLastUserError(EINVAL);
        return EINVAL;
    }

    int res = CdSetGraphicsObject(dc, obj);

    KeDerefObject(obj);
    KeDerefObject(dc);

    if (res != 0) {
        KeSetLastUserError(res);
    }
    return res;
}

export pageableuserexec int SetGraphicsObject(dc_t dc, void* obj) {
    return (int) SystemCall3(SYS_SetGraphicsObject, (size_t) dc, (size_t) obj);
}
