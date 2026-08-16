#include "../api.h"
#include "../clipdraw_internal.h"
#include <obj.h>

export pageable int CdPaintRoundedRectWithBrush(struct dc* dc, int x, int y, int width, int height, 
    int radius, struct brush* brush) {
    struct region rgn = CdCreateRoundedRectRegion(x, y, width, height, radius);
    CdPaintRegionWithBrush(dc, rgn, brush);
    CdFreeRegion(rgn);
    return 0;
}

export pageable int CdPaintRoundedRect(struct dc* dc, int x, int y, int width, int height, 
    int radius) {
    struct brush* brush = CdGetGraphicsObject(dc, UOBJ_BRUSH);
    int retv = CdPaintRoundedRectWithBrush(dc, x, y, width, height, radius, brush);
    DerefObject(brush);
    return retv;
}

export pageable int CdPaintEllipseWithBrush(struct dc* dc, int x, int y, int width, int height, 
    struct brush* brush) {
    struct region rgn = CdCreateEllipseRegion(x, y, width, height);
    CdPaintRegionWithBrush(dc, rgn, brush);
    CdFreeRegion(rgn);
    return 0;
}

export pageable int CdPaintEllipse(struct dc* dc, int x, int y, int width, int height) {
    struct brush* brush = CdGetGraphicsObject(dc, UOBJ_BRUSH);
    int retv = CdPaintEllipseWithBrush(dc, x, y, width, height, brush);
    DerefObject(brush);
    return retv;
}

export pageable int CdPaintPolygonWithBrush(struct dc* dc, int* x, int* y, int points, int mode, 
    struct brush* brush) {
    int count = points;
    struct region rgn = CdCreatePolyPolygonRegion(x, y, &count, 1, mode);
    CdPaintRegionWithBrush(dc, rgn, brush);
    CdFreeRegion(rgn);
    return 0;
}

export pageable int CdPaintPolygon(struct dc* dc, int* x, int* y, int points, int mode) {
    struct brush* brush = CdGetGraphicsObject(dc, UOBJ_BRUSH);
    int retv = CdPaintPolygonWithBrush(dc, x, y, points, mode, brush);
    DerefObject(brush);
    return retv;
}
