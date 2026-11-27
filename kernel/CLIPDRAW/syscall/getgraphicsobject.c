#include <common.h>
#include <obj.h>
#include <string.h>
#include <syscall.h>
#include "../api.h"
#include "../clipdraw_internal.h"

/*
 * `type` is OBJ_BRUSH or OBJ_PEN. Returns a handle to the object currently
 * selected into the DC, or NULL on failure (with the last error set).
 *
 * The caller gets a new handle of its own, and must Deref() it when finished,
 * even if it already had one: it is not the one it selected the object with, and
 * that one can have been closed since (it doesn't matter, as the DC keeps the
 * object alive). The exception is the stock brushes and pens, whose handles are
 * permanent and always the same, so they are handed out as they are.
 */
pageable size_t SysGetGraphicsObject(size_t dc_handle, size_t type, size_t, size_t) {
    if (type != OBJ_BRUSH && type != OBJ_PEN) {
        KeSetLastUserError(EINVAL);
        return 0;
    }
    struct dc* dc = KeValidateUserObjectAndAtomicallyRef((void*) dc_handle, OBJ_DC);
    if (dc == NULL) {
        KeSetLastUserError(EINVAL);
        return 0;
    }

    /* Comes back with a kernel reference, which keeps it alive for us below. */
    void* obj = CdGetGraphicsObject(dc, (int) type);
    KeDerefObject(dc);
    if (obj == NULL) {
        KeSetLastUserError(EINVAL);
        return 0;
    }

    struct user_obj_ref* stock = (type == OBJ_BRUSH) ? CdFindStockBrushHandle(obj) : CdFindStockPenHandle(obj);
    if (stock != NULL) {
        KeDerefObject(obj);
        return (size_t) stock;
    }

    /* Takes over the reference that we have, and gives it back if it can't make the handle. */
    return (size_t) KeCreateUserRef(obj);
}

export pageableuserexec any_t GetGraphicsObject(dc_t dc, int type) {
    return (any_t) SystemCall3(SYS_GetGraphicsObject, (size_t) dc, type);
}
