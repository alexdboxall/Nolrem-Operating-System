#include <log.h>
#include "api.h"
#include <dc.h>
#include <obj.h>
#include <kgfx.h>
#include <errno.h>
#include "clipdraw_internal.h"

export struct compat_bitmap* CdCreateCompatibleBitmapStretched(struct dc* dc, uint8_t* bmp, int w, int h) {
    struct graphics_driver* drv = GetOutputDriver(dc);
    return drv->create_compatible_bitmap(drv, bmp, w, h);
}

export struct compat_bitmap* CdCreateCompatibleBitmap(struct dc* dc, uint8_t* bmp) {
    return CdCreateCompatibleBitmapStretched(dc, bmp, -1, -1);
}


struct context {
    struct dc* dc;
    struct point dest;
    struct compat_bitmap* bmp;
};

static int PaintBitmapCallback(struct rect r, void* _ctxt, int rv, bool* cancel) {
    struct context* ctxt = _ctxt;
    struct rect src;
    src.w = r.w;
    src.h = r.h;
    src.x = r.x - ctxt->dest.x;
    src.y = r.y - ctxt->dest.y;

    struct point dest;
    dest.x = r.x;
    dest.y = r.y;

    ActualBitmapBlit(ctxt->dc, ctxt->bmp, src, dest);
    *cancel = false;
    return rv;
}

export int CdPaintBitmap(struct dc* dc, struct compat_bitmap* bitmap, struct point dest) {
    struct context ctxt;
    ctxt.dc = dc;
    ctxt.dest = dest;
    ctxt.bmp = bitmap;

    struct rect r;
    r.x = dest.x;
    r.y = dest.y;
    r.w = bitmap->width;
    r.h = bitmap->height;
    struct region rrgn = CdCreateRectRegionIndirect(r);
    struct region clipped = CdIntersectWithClipRegion(dc, rrgn);
    CdFreeRegion(rrgn);
    int retv = IterateRegion(
        clipped, 
        PaintBitmapCallback,
        &ctxt,
        0
    );
    CdFreeRegion(clipped);
    return retv;
}