#include <log.h>
#include "api.h"
#include <dc.h>
#include <obj.h>
#include <kgfx.h>
#include <errno.h>
#include "clipdraw_internal.h"

export struct compat_bitmap* CdCreateCompatibleBitmapStretched(struct dc* dc, struct file* bmp, int w, int h) {
    struct graphics_driver* drv = GetOutputDriver(dc);
    return drv->create_compatible_bitmap(drv, bmp, w, h);
}

export struct compat_bitmap* CdCreateCompatibleBitmap(struct dc* dc, struct file* bmp) {
    return CdCreateCompatibleBitmapStretched(dc, bmp, -1, -1);
}

struct context {
    struct dc* dc;
    struct point dest;       // Top-left destination corner on the DC
    struct rect src_rect;    // The sub-rectangle within the bitmap to paint
    struct compat_bitmap* bmp;
};

static int PaintBitmapPartialCallback(struct rect r, void* _ctxt, int rv, bool* cancel) {
    struct context* ctxt = _ctxt;
    struct rect src;
    
    // Calculate how much the current clipped rectangle (r) is offset 
    // from the starting destination point, and apply that same offset to src_rect
    src.x = ctxt->src_rect.x + (r.x - ctxt->dest.x);
    src.y = ctxt->src_rect.y + (r.y - ctxt->dest.y);
    src.w = r.w;
    src.h = r.h;

    struct point dest;
    dest.x = r.x;
    dest.y = r.y;

    ActualBitmapBlit(ctxt->dc, ctxt->bmp, src, dest);
    *cancel = false;
    return rv;
}

export int CdPaintBitmapPartial(struct dc* dc, struct compat_bitmap* bitmap, struct point dest, struct rect src_rect) {
    struct context ctxt;
    ctxt.dc = dc;
    ctxt.dest = dest;
    ctxt.src_rect = src_rect;
    ctxt.bmp = bitmap;

    // The bounding box in the destination DC matches the width and height of the source rectangle
    struct rect r;
    r.x = dest.x;
    r.y = dest.y;
    r.w = src_rect.w;
    r.h = src_rect.h;

    struct region rrgn = CdCreateRectRegionIndirect(r);
    struct region clipped = CdIntersectWithClipRegion(dc, rrgn);
    CdFreeRegion(rrgn);

    int retv = IterateRegion(clipped, PaintBitmapPartialCallback, &ctxt, 0);
    CdFreeRegion(clipped);

    return retv;
}

export int CdPaintBitmap(struct dc* dc, struct compat_bitmap* bitmap, struct point dest) {
    return CdPaintBitmapPartial(dc, bitmap, dest, (struct rect) {
        .x = 0, 
        .y = 0, 
        .w = bitmap->width, 
        .h = bitmap->height
    });
}
