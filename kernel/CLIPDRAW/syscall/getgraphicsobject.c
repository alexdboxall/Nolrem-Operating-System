#include <common.h>
#include <obj.h>
#include <string.h>
#include <syscall.h>
#include "../api.h"
#include "../clipdraw_internal.h"

/*
 * `type` is UOBJ_BRUSH or UOBJ_PEN. Returns the object currently selected into
 * the DC, or NULL on failure (with the last error set).
 *
 * The caller gets its own user reference, and must Deref() it when finished.
 * The exception is the stock brushes and pens, which are permanent and ignore
 * Ref/Deref, so they are handed out without a new reference.
 *
 * Fails with EGONE if the object has already had every user reference dropped
 * (e.g. SetGraphicsObject() then Deref(), leaving the DC as the only owner).
 * There is no handle left to give back, as a released object can't be revived.
 */
pageable size_t SysGetGraphicsObject(size_t dc_ptr, size_t type, size_t, size_t) {
    struct dc* dc = (struct dc*) dc_ptr;

    if (type != UOBJ_BRUSH && type != UOBJ_PEN) {
        KeSetLastUserError(EINVAL);
        return 0;
    }
    if (!ValidateUserObjectAndAtomicallyRef(dc, UOBJ_DC)) {
        KeSetLastUserError(EINVAL);
        return 0;
    }

    /* Comes back with a kernel reference, which keeps it alive for us below. */
    void* obj = CdGetGraphicsObject(dc, (int) type);
    DerefObject(dc);
    if (obj == NULL) {
        KeSetLastUserError(EINVAL);
        return 0;
    }

    bool is_stock = (type == UOBJ_BRUSH) ? CdIsStockBrush(obj) : CdIsStockPen(obj);
    if (!is_stock) {
        int res = UserRef(obj);
        if (res != 0) {
            DerefObject(obj);
            KeSetLastUserError(res);
            return 0;
        }
    }

    DerefObject(obj);
    return (size_t) obj;
}

export pageableuserexec any_t GetGraphicsObject(dc_t dc, int type) {
    return (any_t) SystemCall3(SYS_GetGraphicsObject, (size_t) dc, type);
}
