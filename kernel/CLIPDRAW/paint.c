#include <log.h>
#include "api.h"
#include <dc.h>
#include <obj.h>
#include <kgfx.h>
#include <errno.h>
#include "clipdraw_internal.h"

struct context {
    struct dc* dc;
    union {
        struct brush* brush;
        pen_t pen;
    };
};


static int PaintRectCallbackPreTransformed(struct rect r, void* _ctxt, int rv, bool* cancel) {
    struct context* ctxt = _ctxt;
    int x1 = r.x;
    int y1 = r.y;
    int x2 = r.x + r.w;
    int y2 = r.y + r.h;
    ActualPaintRectWithBrush(ctxt->dc, x1, y1, x2, y2, ctxt->brush);
    *cancel = false;
    return rv;
}

static int InvertRectCallback(struct rect r, void* _ctxt, int rv, bool* cancel) {
    struct dc* dc = _ctxt;
    int x1 = r.x;
    int y1 = r.y;
    int x2 = r.x + r.w;
    int y2 = r.y + r.h;
    ActualInvertRect(dc, x1, y1, x2, y2, false);
    *cancel = false;
    return rv;
}

static int CdPaintRegionWithBrushPreTransformed(struct dc* dc, struct region rgn, struct brush* brush) {
    struct context ctxt;
    ctxt.dc = dc;
    ctxt.brush = brush;
    return IterateRegion(
        rgn, 
        PaintRectCallbackPreTransformed,
        &ctxt,
        0
    );
}

export int CdPaintRegionWithBrush(struct dc* dc, struct region rgn, struct brush* brush) {
    struct context ctxt;
    ctxt.dc = dc;
    ctxt.brush = brush;
    struct region clipped = CdIntersectWithClipRegion(dc, rgn);
    int retv = IterateRegion(
        clipped, 
        PaintRectCallbackPreTransformed,
        &ctxt,
        0
    );
    CdFreeRegion(clipped);
    return retv;
}

export int CdInvertRegion(struct dc* dc, struct region rgn) {
    struct region clipped = CdIntersectWithClipRegion(dc, rgn);
    int retv = IterateRegion(
        rgn, 
        InvertRectCallback,
        dc,
        0
    );
    CdFreeRegion(clipped);
    return retv;
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

export int CdPaintGradientRectHz(struct dc* dc, int x, int y, int width, int height, colour_t c1, colour_t c2) {
    /* Prevent division by zero or dodgy negative stuff. */
    if (width <= 0 || height <= 0) {
        return EINVAL;
    }

    int32_t r1 = GetRed(c1);
    int32_t g1 = GetGreen(c1);
    int32_t b1 = GetBlue(c1);
    int32_t a1 = GetAlpha(c1);
    int32_t r2 = GetRed(c2);
    int32_t g2 = GetGreen(c2);
    int32_t b2 = GetBlue(c2);
    int32_t a2 = GetAlpha(c2);

    r1 <<= 16;
    g1 <<= 16;
    b1 <<= 16;
    a1 <<= 16;
    r2 <<= 16;
    g2 <<= 16;
    b2 <<= 16;
    a2 <<= 16;

    int32_t adj_r = (r2 - r1) / width;
    int32_t adj_g = (g2 - g1) / width;
    int32_t adj_b = (b2 - b1) / width;
    int32_t adj_a = (a2 - a1) / width;

    struct brush* grad_brush = CdCreateSolidBrush(c1);
    for (int i = 0; i < width; ++i) {
        uint32_t new_r = CLAMP(r1, 0, 255 * 65536) >> 16;
        uint32_t new_g = CLAMP(g1, 0, 255 * 65536) >> 16;
        uint32_t new_b = CLAMP(b1, 0, 255 * 65536) >> 16;
        uint32_t new_a = CLAMP(a1, 0, 255 * 65536) >> 16;
        colour_t new_col = (new_a << 24) | (new_r << 16) | (new_g << 8) | new_b;
        CdSetBrushColour(grad_brush, new_col);
        CdPaintRectWithBrush(dc, x + i, y, 1, height, grad_brush);
        r1 += adj_r;
        g1 += adj_g;
        b1 += adj_b;
        a1 += adj_a;
    }
    DerefObject(grad_brush);
    return 0;
}

export int CdInvertRect(struct dc* dc, int x, int y, int width, int height) {
    struct region rr = CdCreateRectRegion(x, y, width, height);
    struct region rgn = CdIntersectWithClipRegion(dc, rr);
    CdFreeRegion(rr);
    CdInvertRegion(dc, rgn);
    CdFreeRegion(rgn);
    return 0;
}

export int CdPaintRectWithBrush(struct dc* dc, int x, int y, int width, int height, 
    struct brush* brush) {
    struct region rr = CdCreateRectRegion(x, y, width, height);
    struct region rgn = CdIntersectWithClipRegion(dc, rr);
    CdFreeRegion(rr);
    CdPaintRegionWithBrushPreTransformed(dc, rgn, brush);
    CdFreeRegion(rgn);
    return 0;
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