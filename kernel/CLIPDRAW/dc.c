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
    struct user_obj_header hdr;
    struct graphics_driver* drv;
    struct brush* brush;
    struct pen* pen;
};

static struct brush* dummy_brush;

struct graphics_driver* GetOutputDriver(struct dc* dc) {
    return dc->drv;
}

export int CdSetGraphicsObject(struct dc* dc, void* obj) {
    struct user_obj_header* hdr = obj;
    switch (hdr->user_type) {
    case UOBJ_BRUSH:
        RefObject(obj);
        DerefObject(dc->brush);
        dc->brush = obj;
        return 0;
    case UOBJ_PEN:
        RefObject(obj);
        DerefObject(dc->pen);
        dc->pen = obj;
        return 0;
    default:
        return EINVAL;
    }
}

export void* CdGetGraphicsObject(struct dc* dc, int type) {
    switch (type) {
    case UOBJ_BRUSH:
        RefObject(dc->brush);
        return dc->brush;
    case UOBJ_PEN:
        RefObject(dc->pen);
        return dc->pen;
    default:
        return NULL;
    }
}

void CleanupDc(void* _dc) {
    struct dc* dc = _dc;
    DerefObject(dc->brush);
    FreeHeap(dc);
}

void InitDc(void) {
    RegisterUserObjectType(UOBJ_REGION, CleanupDc);
    dummy_brush = CdCreateSolidBrush(SystemColour());
}

#define OBJTYPE_DUMMY 20

void CleanupDummy(void*) {

}

export struct dc* CdCreateDc(void) {
    struct dc* dc = AllocHeap(sizeof(struct dc));
    InitUserObject(&dc, UOBJ_REGION);
    dc->drv = GetKernelGraphicsDriver();
    dc->brush = dummy_brush;
    dc->pen = NULL;
    AddGraphicsFallbacksWhereNeeded(dc->drv);
    return dc;
}
