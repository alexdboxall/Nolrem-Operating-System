#include "api.h"

#include <common.h>
#include <errno.h>
#include <log.h>
#include <obj.h>
#include <kgfx.h>
#include <heap.h>
#include <dc.h>
#include "clipdraw_internal.h"

struct dc {
    struct obj_header hdr;
    struct spinlock lock;
    struct graphics_driver* drv;
    struct brush* brush;
    struct pen* pen;
    struct region cliprgn;

    // for subwindowing
    int16_t trans_x;
    int16_t trans_y;
};

export struct graphics_driver* CdGetOutputDriver(struct dc* dc) {
    return dc->drv;
}

void CdMapDCCoordinates(struct dc* dc, int* x1, int* y1, int* x2, int* y2) {
    // TODO: the future add viewport/window origin/extents
    KeAcquireSpinlock(&dc->lock);
    *x1 += dc->trans_x;
    *x2 += dc->trans_x;
    *y1 += dc->trans_y;
    *y2 += dc->trans_y;
    KeReleaseSpinlock(&dc->lock);
}


void CdLogicalToDevice(struct dc* dc, int* x, int* y) {
    int dummy;
    CdMapDCCoordinates(dc, x, y, &dummy, &dummy);
}


export void CdSetTranslation(struct dc* dc, int x, int y) {
    KeAcquireSpinlock(&dc->lock);
    dc->trans_x = x;
    dc->trans_y = y;
    KeReleaseSpinlock(&dc->lock);
}

// Makes a COPY of the input region
export int CdSetClipRegion(struct dc* dc, struct region rgn) {
    KeAcquireSpinlock(&dc->lock);
    CdFreeRegion(dc->cliprgn);
    dc->cliprgn = CdCopyRegion(rgn);
    KeReleaseSpinlock(&dc->lock);
    return 0;
}

export int CdRestrictClipRegion(struct dc* dc, struct region rgn) {
    KeAcquireSpinlock(&dc->lock);
    CdIntersectRegionInPlace(&dc->cliprgn, rgn);
    KeReleaseSpinlock(&dc->lock);
    return 0;
}

export struct region CdGetCopyOfClipRegion(struct dc* dc) {
    KeAcquireSpinlock(&dc->lock);
    struct region retv = CdCopyRegion(dc->cliprgn);
    KeReleaseSpinlock(&dc->lock);
    return retv;
}

// TODO: this function needs to become the 'guts' of the gfx driver interface
//       functions, doing the scaling of the input `rgn` in some sort of
//       CdGetRegionCombinationEx(op, a, b, dc) that uses DC coords on `b`
//       and apply dc->trans_x, dc->trans_y as  a simple offset
export struct region CdIntersectWithClipRegion(struct dc* dc, struct region rgn) {
    //KeAcquireSpinlock(&dc->lock);
    // TODO: the DC gets locked on scaling within this function.
    // might need to pass in the scale params in a struct?
    // copying the DC or the region sound slow
    struct region retv = CdGetRegionCombinationEx(REGION_COMBINE_INTERSECT, dc->cliprgn, rgn, dc);
    //KeReleaseSpinlock(&dc->lock);
    return retv;
}

export int CdSetGraphicsObject(struct dc* dc, void* obj) {
    KeAcquireSpinlock(&dc->lock);
    switch (KeGetObjectType(obj)) {
    case OBJ_BRUSH:
        KeRefObject(obj);
        KeDerefObject(dc->brush);
        dc->brush = obj;
        KeReleaseSpinlock(&dc->lock);
        return 0;
    case OBJ_PEN:
        KeRefObject(obj);
        KeDerefObject(dc->pen);
        dc->pen = obj;
        KeReleaseSpinlock(&dc->lock);
        return 0;
    default:
        KeReleaseSpinlock(&dc->lock);
        return EINVAL;
    }
}

export void* CdGetGraphicsObject(struct dc* dc, int type) {
    void* retv = NULL;
    KeAcquireSpinlock(&dc->lock);
    switch (type) {
    case OBJ_BRUSH:
        retv = dc->brush;
        break;
    case OBJ_PEN:
        retv = dc->pen;
        break;
    default:
        KeReleaseSpinlock(&dc->lock);
        return NULL;
    }
    KeRefObject(retv);
    KeReleaseSpinlock(&dc->lock);
    return retv;
}

void CdCleanupDc(void* _dc) {
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
    KeRegisterObjectType(OBJ_DC, CdCleanupDc);
}

export struct dc* CdCreateDc(void) {
    struct dc* dc = AllocHeap(sizeof(struct dc));
    if (dc == NULL) {
        return NULL;
    }
    KeInitObject(dc, OBJ_DC);
    KeInitSpinlock(&dc->lock);
    dc->drv = KeGetKernelGraphicsDriver();    
    dc->brush = CdGetStockBrush(STOCK_BRUSH_SYSTEM);
    dc->pen = CdGetStockPen(STOCK_PEN_BLACK_1);
    // CdGetStockBrush doesn't add a ref, but on brush/pen change we deref,
    // so we need to ref here
    KeRefObject(dc->brush);
    KeRefObject(dc->pen);
    dc->cliprgn = CdEverythingRegion();
    if (CdIsRegionBad(dc->cliprgn)) {
        /* No memory for the clip region. Give back the references taken above. */
        KeDerefObject(dc->brush);
        KeDerefObject(dc->pen);
        FreeHeap(dc);
        return NULL;
    }
    dc->trans_x = 0;
    dc->trans_y = 0;
    CdAddGraphicsFallbacksWhereNeeded(dc->drv);
    return dc;
}

export void CdResetDC(struct dc* dc) {
    KeAcquireSpinlock(&dc->lock);
    
    CdFreeRegion(dc->cliprgn);
    dc->cliprgn = CdEverythingRegion();
    dc->trans_x = 0;
    dc->trans_y = 0;

    dc->drv = KeGetKernelGraphicsDriver();
    CdAddGraphicsFallbacksWhereNeeded(dc->drv);

    KeDerefObject(dc->brush);
    KeDerefObject(dc->pen);
    dc->brush = CdGetStockBrush(STOCK_BRUSH_SYSTEM);
    dc->pen = CdGetStockPen(STOCK_PEN_BLACK_1);
    KeRefObject(dc->brush);
    KeRefObject(dc->pen);

    KeReleaseSpinlock(&dc->lock);
}