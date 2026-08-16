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
};

static struct brush* dummy_brush;
static struct pen* dummy_pen;

struct graphics_driver* GetOutputDriver(struct dc* dc) {
    return dc->drv;
}

export int CdSetGraphicsObject(struct dc* dc, void* obj) {
    LockUserObject(dc);
    struct user_obj_header* hdr = obj;
    switch (hdr->user_type) {
    case UOBJ_BRUSH:
        RefObject(obj);
        DerefObject(dc->brush);
        dc->brush = obj;
        UnlockUserObject(dc);
        return 0;
    case UOBJ_PEN:
        RefObject(obj);
        DerefObject(dc->pen);
        dc->pen = obj;
        UnlockUserObject(dc);
        return 0;
    default:
        UnlockUserObject(dc);
        return EINVAL;
    }
}

export void* CdGetGraphicsObject(struct dc* dc, int type) {
    void* retv = NULL;
    LockUserObject(dc);
    switch (type) {
    case UOBJ_BRUSH:
        retv = dc->brush;
        break;
    case UOBJ_PEN:
        retv = dc->pen;
        break;
    default:
        UnlockUserObject(dc);
        return NULL;
    }
    RefObject(retv);
    UnlockUserObject(dc);
    return retv;
}

void CleanupDc(void* _dc) {
    struct dc* dc = _dc;
    DerefObject(dc->brush);
    FreeHeap(dc);
}

void CdInitDcSubsystem(void) {
    RegisterUserObjectType(UOBJ_REGION, CleanupDc);
    dummy_brush = CdCreateSolidBrush(SystemColour());
    dummy_pen = CdCopyPen(CdGetStockPen(STOCK_PEN_BLACK_1));
}

export struct dc* CdCreateDc(void) {
    struct dc* dc = AllocHeap(sizeof(struct dc));
    InitUserObject(&dc, UOBJ_REGION);
    dc->drv = GetKernelGraphicsDriver();
    dc->brush = dummy_brush;
    dc->pen = NULL;// dummy_pen;
    AddGraphicsFallbacksWhereNeeded(dc->drv);
    return dc;
}
