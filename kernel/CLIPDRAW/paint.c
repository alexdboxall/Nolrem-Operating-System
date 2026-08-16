#include "api.h"
#include <dc.h>
#include <obj.h>
#include <kgfx.h>
#include "clipdraw_internal.h"

struct context {
    struct dc* dc;
    union {
        struct brush* brush;
        pen_t pen;
    };
};

static int PaintRectCallback(struct rect r, void* _ctxt, int rv, bool* cancel) {
    struct context* ctxt = _ctxt;
    CdPaintRectWithBrush(ctxt->dc, r.x, r.y, r.w, r.h, ctxt->brush);
    *cancel = false;
    return rv;
}

static int InvertRectCallback(struct rect r, void* _ctxt, int rv, bool* cancel) {
    struct dc* dc = _ctxt;
    CdInvertRect(dc, r.x, r.y, r.w, r.h);
    *cancel = false;
    return rv;
}

export int CdPaintRegionWithBrush(struct dc* dc, struct region rgn, struct brush* brush) {
    struct context ctxt;
    ctxt.dc = dc;
    ctxt.brush = brush;
    return IterateRegion(
        rgn, 
        PaintRectCallback,
        &ctxt,
        0
    );
}

export int CdInvertRegion(struct dc* dc, struct region rgn) {
    return IterateRegion(
        rgn, 
        InvertRectCallback,
        dc,
        0
    );
}

export int CdPaintRegion(struct dc* dc, struct region rgn) {
    struct brush* brush = CdGetGraphicsObject(dc, UOBJ_BRUSH);
    int retv = CdPaintRegionWithBrush(dc, rgn, brush);
    DerefObject(brush);
    return retv;
}

export int CdPaintRect(struct dc* dc, int x, int y, int width, int height) {
    struct brush* brush = CdGetGraphicsObject(dc, UOBJ_BRUSH);
    int retv = CdPaintRectWithBrush(dc, x, y, width, height, brush);
    DerefObject(brush);
    return retv;
}




/*
struct dc* PaintBitmap(struct dc* dc, struct dc* bitmap, int x, int y);
int PaintTextWithFont(struct dc* dc, const char* text, int x, int y, font_t font);
int PaintText(struct dc* dc, const char* text, int x, int y);
int GreyText(struct dc* dc, const char* text, int x, int y);
int PaintLineWithPen(struct dc* dc, int x1, int y1, int x2, int y2, pen_t pen);
int PaintLine(struct dc* dc, int x1, int y1, int x2, int y2);
int FloodFill(struct dc* dc, int x, int y, colour_t col, int mode);
int FrameRectWithPen(struct dc* dc, int x, int y, int w, int h, pen_t pen);
int FrameRect(struct dc* dc, int x, int y, int w, int h);
int FrameRegionWithPen(struct dc* dc, struct region rgn, pen_t pen);
int FrameRegion(struct dc* dc, struct region rgn);*/