#include "api.h"

#include <common.h>
#include <log.h>
#include <obj.h>
#include <kgfx.h>
#include <heap.h>
#include <dc.h>
#include "clipdraw_internal.h"

struct dc {
    struct obj_header hdr;
    struct graphics_driver* drv;
    struct brush* brush;
    struct pen* pen;
};

static struct brush* dummy_brush;

export struct brush* CdGetDcBrush(struct dc* dc) {
    RefObject(dc->brush);
    return dc->brush;
}

struct graphics_driver* GetOutputDriver(struct dc* dc) {
    return dc->drv;
}

export void CdSetDcBrush(dc_t dc, brush_t brush) {
    RefObject(brush);
    DerefObject(dc->brush);
    dc->brush = brush;
}

void CleanupDc(void* _dc) {
    struct dc* dc = _dc;
    DerefObject(dc->brush);
    FreeHeap(dc);
}

void InitDc(void) {
    RegisterObjectType(OBJTYPE_DC, CleanupDc);
    dummy_brush = CdCreateSolidBrush(SystemColour());
}

#define OBJTYPE_DUMMY 20

void CleanupDummy(void*) {

}

export struct dc* CdCreateDc(void) {
    struct dc* dc = AllocHeap(sizeof(struct dc));
    InitObject(&dc, OBJTYPE_DC);
    dc->drv = GetKernelGraphicsDriver();
    dc->brush = dummy_brush;
    dc->pen = NULL;
    AddGraphicsFallbacksWhereNeeded(dc->drv);
    return dc;
}
