#include "api.h"

#include <common.h>
#include <errno.h>
#include <log.h>
#include <obj.h>
#include <kgfx.h>
#include <heap.h>
#include <dc.h>
#include "clipdraw_internal.h"

// TODO: LOCKS!

struct dc {
    struct user_obj_header hdr;
    struct graphics_driver* drv;
    struct brush* brush;
    struct pen* pen;
    struct region cliprgn;

    // for subwindowing
    int16_t trans_x;
    int16_t trans_y;
};

struct graphics_driver* GetOutputDriver(struct dc* dc) {
    return dc->drv;
}

void CdMapDCCoordinates(struct dc* dc, int* x1, int* y1, int* x2, int* y2) {
    // TODO: the future add viewport/window origin/extents
    KeLockUserObject(dc);
    *x1 += dc->trans_x;
    *x2 += dc->trans_x;
    *y1 += dc->trans_y;
    *y2 += dc->trans_y;
    KeUnlockUserObject(dc);
}


void CdLogicalToDevice(struct dc* dc, int* x, int* y) {
    int dummy;
    CdMapDCCoordinates(dc, x, y, &dummy, &dummy);
}


void CdSetTranslation(struct dc* dc, int x, int y) {
    KeLockUserObject(dc);
    dc->trans_x = x;
    dc->trans_y = y;
    KeUnlockUserObject(dc);
}

// Makes a COPY of the input region
export int CdSetClipRegion(struct dc* dc, struct region rgn) {
    KeLockUserObject(dc);
    CdFreeRegion(dc->cliprgn);
    dc->cliprgn = CdCopyRegion(rgn);
    KeUnlockUserObject(dc);
    return 0;
}

export int CdRestrictClipRegion(struct dc* dc, struct region rgn) {
    KeLockUserObject(dc);
    CdIntersectRegionInPlace(&dc->cliprgn, rgn);
    KeUnlockUserObject(dc);
    return 0;
}

export struct region CdGetCopyOfClipRegion(struct dc* dc) {
    KeLockUserObject(dc);
    struct region retv = CdCopyRegion(dc->cliprgn);
    KeUnlockUserObject(dc);
    return retv;
}

// TODO: this function needs to become the 'guts' of the gfx driver interface
//       functions, doing the scaling of the input `rgn` in some sort of
//       CdGetRegionCombinationEx(op, a, b, dc) that uses DC coords on `b`
//       and apply dc->trans_x, dc->trans_y as  a simple offset
export struct region CdIntersectWithClipRegion(struct dc* dc, struct region rgn) {
    //KeLockUserObject(dc);
    // TODO: the DC gets locked on scaling within this function.
    // might need to pass in the scale params in a struct?
    // copying the DC or the region sound slow
    struct region retv = CdGetRegionCombinationEx(REGION_COMBINE_INTERSECT, dc->cliprgn, rgn, dc);
    //KeUnlockUserObject(dc);
    return retv;
}

export int CdSetGraphicsObject(struct dc* dc, void* obj) {
    KeLockUserObject(dc);
    struct user_obj_header* hdr = obj;
    switch (hdr->user_type) {
    case UOBJ_BRUSH:
        KeRefObject(obj);
        KeDerefObject(dc->brush);
        dc->brush = obj;
        KeUnlockUserObject(dc);
        return 0;
    case UOBJ_PEN:
        KeRefObject(obj);
        KeDerefObject(dc->pen);
        dc->pen = obj;
        KeUnlockUserObject(dc);
        return 0;
    default:
        KeUnlockUserObject(dc);
        return EINVAL;
    }
}

export void* CdGetGraphicsObject(struct dc* dc, int type) {
    void* retv = NULL;
    KeLockUserObject(dc);
    switch (type) {
    case UOBJ_BRUSH:
        retv = dc->brush;
        break;
    case UOBJ_PEN:
        retv = dc->pen;
        break;
    default:
        KeUnlockUserObject(dc);
        return NULL;
    }
    KeRefObject(retv);
    KeUnlockUserObject(dc);
    return retv;
}

void CleanupDc(void* _dc) {
    struct dc* dc = _dc;
    if (dc == NULL) {
        return;
    }
    KeDerefObject(dc->brush);
    KeDerefObject(dc->pen);
    CdFreeRegion(dc->cliprgn);
    FreeHeap(dc);
}

void CdInitDcSubsystem(void) {
    KeRegisterUserObjectType(UOBJ_DC, CleanupDc);
}

export struct dc* CdCreateDc(void) {
    struct dc* dc = AllocHeap(sizeof(struct dc));
    if (dc == NULL) {
        return NULL;
    }
    KeInitUserObject(dc, UOBJ_DC);
    dc->drv = KeGetKernelGraphicsDriver();    
    dc->brush = CdGetStockBrush(STOCK_BRUSH_SYSTEM);
    dc->pen = CdGetStockPen(STOCK_PEN_BLACK_1);
    // CdGetStockBrush doesn't add a ref, but on brush/pen change we deref,
    // so we need to ref here
    KeRefObject(dc->brush);
    KeRefObject(dc->pen);
    dc->cliprgn = CdEverythingRegion();
    dc->trans_x = 0;
    dc->trans_y = 0;
    AddGraphicsFallbacksWhereNeeded(dc->drv);
    return dc;
}

void CdResetDC(struct dc* dc) {
    KeLockUserObject(dc);
    
    CdFreeRegion(dc->cliprgn);
    dc->cliprgn = CdEverythingRegion();
    dc->trans_x = 0;
    dc->trans_y = 0;

    dc->drv = KeGetKernelGraphicsDriver();
    AddGraphicsFallbacksWhereNeeded(dc->drv);

    KeDerefObject(dc->brush);
    KeDerefObject(dc->pen);
    dc->brush = CdGetStockBrush(STOCK_BRUSH_SYSTEM);
    dc->pen = CdGetStockPen(STOCK_PEN_BLACK_1);
    KeRefObject(dc->brush);
    KeRefObject(dc->pen);

    KeUnlockUserObject(dc);
}