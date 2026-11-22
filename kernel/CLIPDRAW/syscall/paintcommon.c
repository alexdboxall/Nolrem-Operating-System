#include <common.h>
#include <obj.h>
#include <string.h>
#include <transfer.h>
#include <syscall.h>
#include "../api.h"
#include "../clipdraw_internal.h"

/*
 * Helpers shared by the painting syscalls. This file contains no syscall itself.
 */

/*
 * Copies a struct user_shape_args in from user mode and checks it. The flags are
 * checked here; the shape itself (and the radius, where there is one) is the
 * caller's to check, as what counts as valid depends on the shape.
 *
 * Returns 0 on success, or an errno.
 */
pageable int CdReadShapeArgs(size_t user_ptr, struct user_shape_args* out) {
    struct transfer tr = KeCreateTransferReadingFromUser((const void*) user_ptr, sizeof(struct user_shape_args), 0);
    int res = KePerformTransfer(out, &tr, sizeof(struct user_shape_args));
    if (res != 0) {
        return res;
    }
    if (out->flags & ~PAINT_USE_DC_BRUSH) {
        return EINVAL;
    }
    if (!CdIsShapeInRange(out->x, out->y, out->w, out->h)) {
        return EINVAL;
    }
    return 0;
}

/*
 * Validates and takes a reference to the DC, and to the brush unless the flags
 * say to use the DC's own. With PAINT_USE_DC_BRUSH `*brush_out` is NULL, which
 * is how the caller knows to use the plain Cd function.
 *
 * On success, call CdReleasePaintObjects() when finished. On failure nothing is
 * held, and the return value is an errno.
 */
pageable int CdAcquirePaintObjects(void* dc_ptr, struct brush* brush_ptr, int flags,
    struct dc** dc_out, struct brush** brush_out) {

    if (!KeValidateUserObjectAndAtomicallyRef(dc_ptr, UOBJ_DC)) {
        return EINVAL;
    }
    if (!(flags & PAINT_USE_DC_BRUSH)
        && !KeValidateUserObjectAndAtomicallyRef(brush_ptr, UOBJ_BRUSH)) {
        KeDerefObject(dc_ptr);
        return EINVAL;
    }

    *dc_out = dc_ptr;
    *brush_out = (flags & PAINT_USE_DC_BRUSH) ? NULL : brush_ptr;
    return 0;
}

pageable void CdReleasePaintObjects(struct dc* dc, struct brush* brush) {
    if (brush != NULL) {
        KeDerefObject(brush);
    }
    KeDerefObject(dc);
}
