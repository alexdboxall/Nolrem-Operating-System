#include <common.h>
#include <obj.h>
#include <string.h>
#include <syscall.h>
#include "../api.h"
#include "../clipdraw_internal.h"

/*
 * a = DC, rgn_ptr = region, brush_ptr = brush (ignored with PAINT_USE_DC_BRUSH).
 * Returns 0 on success, or an errno. Drawing is clipped to the DC's clip region.
 */
pageable size_t SysPaintRegion(size_t a, size_t rgn_ptr, size_t brush_ptr, size_t flags) {
    struct uregion* urgn = (struct uregion*) rgn_ptr;

    if (KeIsOutOfMemory()) {
        KeSetLastUserError(ENOMEM);
        return ENOMEM;
    }
    if (flags & ~(size_t) PAINT_USE_DC_BRUSH) {
        KeSetLastUserError(EINVAL);
        return EINVAL;
    }
    if (!KeValidateUserObjectAndAtomicallyRef(urgn, UOBJ_REGION)) {
        KeSetLastUserError(EINVAL);
        return EINVAL;
    }

    struct dc* dc;
    struct brush* brush;
    int res = CdAcquirePaintObjects((void*) a, (struct brush*) brush_ptr, (int) flags, &dc, &brush);
    if (res != 0) {
        KeDerefObject(urgn);
        KeSetLastUserError(res);
        return res;
    }

    if (brush != NULL) {
        res = CdPaintRegionWithBrush(dc, UserRegionToRegion(urgn), brush);
    } else {
        res = CdPaintRegion(dc, UserRegionToRegion(urgn));
    }

    CdReleasePaintObjects(dc, brush);
    KeDerefObject(urgn);
    if (res != 0) {
        KeSetLastUserError(res);
    }
    return res;
}

export pageableuserexec int PaintRegionWithBrush(dc_t dc, region_t rgn, brush_t brush) {
    /* Not SystemCall4: that doesn't set the 4th argument (flags). */
    return (int) SystemCall(SYS_PaintRegion, (size_t) dc, (size_t) rgn, (size_t) brush, 0);
}

export pageableuserexec int PaintRegion(dc_t dc, region_t rgn) {
    return (int) SystemCall(SYS_PaintRegion, (size_t) dc, (size_t) rgn, 0, PAINT_USE_DC_BRUSH);
}
