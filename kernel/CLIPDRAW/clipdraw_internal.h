#pragma once

#include "api.h"
#include <obj.h>

/* INTERNAL ONLY */

struct path {
    int num_points_total;       // includes both x,y, and array points

    // the most recently added point
    int x;
    int y;

    // all earlier points (does *NOT* include the one stored in the x,y vars)
    void* data;

    // polygon point counts
    void* count_data;  // completed polygon counts only
    int polygons;      // includes the one currently being worked on
    int count_total_so_far;     // of the ones in the count_data array

    int scale_x_16_16;
    int scale_y_16_16;
    int trans_x;
    int trans_y;
};

struct graphics_driver;
void CdAddGraphicsFallbacksWhereNeeded(struct graphics_driver* drv);

void CdMapDCCoordinates(struct dc* dc, int* x1, int* y1, int* x2, int* y2);
void CdLogicalToDevice(struct dc* dc, int* x, int* y);

struct region_iteration_context;

int CdIterateRegionCoroutine(
    struct region rgn,
    int retv,
    void* context,
    struct region_iteration_context* ctxt,
    bool (*band_callback)(int y0, int y1, int* retv, void* context),                        /* return TRUE to stop early */
    bool (*rect_callback)(int y0, int y1, int x0, int x1, int* retv, void* context),        /* as above */
    struct dc* dc
);

int CdIterateRegion(
    struct region rgn, 
    int (*rect_callback)(struct rect r, void* context, int rv, bool* cancel), 
    void* context,
    int init_rv
);

struct graphics_driver* CdGetOutputDriver(struct dc*);

void CdInitBrushSubsystem(void);
void CdInitUserRegionSubsystem(void);
void CdInitPenSubsystem(void);
void CdInitDcSubsystem(void);
void CdInitMouseSubsystem(void);
void CdInitSyscalls(void);
void CdInitTypefaceSubsystem(void);

void CdPurgeScaledGlyphCacheIfNeeded(void);

void CdRemoveMouse();
void CdDrawMouse(int x, int y);
void CdStartVideoUpdate(int x1, int y1, int x2, int y2); 
void CdEndVideoUpdate(int x1, int y1, int x2, int y2);

struct uregion {
    struct user_obj_header hdr;
    struct region rgn;
};

struct region CdGetRegionCombinationEx(int mode, struct region a, struct region b, struct dc* scale_dc);

struct uregion* CdRegionToUserRegion(struct region rgn);
struct region CdUserRegionToRegion(struct uregion* urgn);

void CdSetTranslation(struct dc* dc, int x, int y);

struct region CdBadRegion(void);
bool CdIsRegionBad(struct region rgn);

/* Set in `flags` of the paint syscalls: ignore `brush` and paint with whichever
 * brush is currently selected into the DC. */
#define PAINT_USE_DC_BRUSH  1

/* Passed by pointer to the paint/invert syscalls that take more parameters than
 * will fit in registers. The user-side wrappers and the kernel side share it. */
struct user_shape_args {
    int x;
    int y;
    int w;
    int h;
    int radius;             /* PaintRoundedRect only */
    struct brush* brush;    /* ignored with PAINT_USE_DC_BRUSH, and by the invert calls */
    int flags;
};

/* Regions only hold 16-bit coordinates, so shapes must fit in that range. This
 * is also the extent of CdEverythingRegion(). */
static inline bool CdIsShapeInRange(int x, int y, int w, int h) {
    return x >= INT16_MIN && x <= INT16_MAX
        && y >= INT16_MIN && y <= INT16_MAX
        && w >= 0 && h >= 0
        && x + w <= INT16_MAX && y + h <= INT16_MAX;
}

/* Helpers for the paint syscalls (syscall/paintcommon.c). All return 0 or an errno. */
int CdReadShapeArgs(size_t user_ptr, struct user_shape_args* out);
int CdAcquirePaintObjects(void* dc_ptr, struct brush* brush_ptr, int flags, 
    struct dc** dc_out, struct brush** brush_out);
void CdReleasePaintObjects(struct dc* dc, struct brush* brush);

/* GCC can actually optimise some of the getter/setter functions if the 
 * same values are placed in the same spot. lol.
 */
struct brush {
    struct user_obj_header hdr;
    uint8_t origin_x;
    uint8_t origin_y;
    uint8_t pattern_type;
    uint8_t pattern[8];
    colour_t primary;
    colour_t secondary;
};

struct pen {
    struct user_obj_header hdr;
    uint8_t origin_x;
    uint8_t origin_y;
    uint8_t pattern_type;
    uint8_t pattern[8];
    colour_t col;
    uint8_t pat_width;
    uint8_t pat_height;
    bool is_custom;
    int thickness;
};



int CdActualPaintRectWithBrush(struct dc* dc, int x, int y, int x2, int y2, 
    struct brush* brush);
int CdActualInvertRect(struct dc* dc, int x, int y, int x2, int y2, bool half);
int CdActualBitmapBlit(struct dc* dc, struct compat_bitmap* bitmap, struct rect src, struct point dest);

void CdResetDC(struct dc* dc);

/* INTERNAL, BUT HAS USER-WRAPPER*/
struct dc* CdCreateDc(void);

int CdSetGraphicsObject(struct dc* dc, void* obj);
void* CdGetGraphicsObject(struct dc* dc, int type);

int CdPaintRegionWithBrush(struct dc* dc, struct region rgn, struct brush* brush);
int CdPaintRegion(struct dc* dc, struct region rgn);
int CdPaintRectWithBrush(struct dc* dc, int x, int y, int width, int height, 
    struct brush* brush);
int CdPaintRect(struct dc* dc, int x, int y, int width, int height);
int CdInvertRect(struct dc* dc, int x, int y, int width, int height);
int CdInvertRegion(struct dc* dc, struct region rgn);

struct region CdEmptyRegion(void);
struct region CdCopyRegion(struct region rgn);
void CdFreeRegion(struct region rgn);
int CdTranslateRegion(struct region* rgn, int offx, int offy);
struct region CdCreateRectRegion(int x, int y, int width, int height);
struct region CdEverythingRegion(void);
struct region CdGetRegionCombination(int mode, struct region a, struct region b);
bool CdIsRegionEmpty(struct region rgn);

static inline struct region CdCreateRectRegionIndirect(struct rect r) {
    return CdCreateRectRegion(
        r.x, r.y, r.w, r.h
    );
}

struct rect CdGetRegionBounds(struct region rgn);

struct brush* CdCreatePatternedBrush(colour_t primary, colour_t secondary, int pattern);
struct brush* CdCreateSolidBrush(colour_t argb);
struct brush* CdCreateCustomBrush(colour_t primary, colour_t secondary, uint8_t* pattern);
int CdSetBrushPattern(struct brush* br, int pattern);
int CdGetBrushPattern(struct brush* br);

int CdSetBrushColour(struct brush* br, colour_t argb);
colour_t CdGetBrushColour(struct brush* br);

int CdSetBrushSecondaryColour(struct brush* br, colour_t argb);
colour_t CdGetBrushSecondaryColour(struct brush* br);

struct brush* CdGetStockBrush(int type);
bool CdIsStockBrush(struct brush* br);

int CdSetBrushOrigin(struct brush* br, int x, int y);
struct point CdGetBrushOrigin(struct brush* br);

struct brush* CdCopyBrush(struct brush* br);

int CdPaintGradientRectHz(struct dc* dc, int x, int y, int width, int height, colour_t c1, colour_t c2);

int CdSetClipRegion(struct dc* dc, struct region rgn);
int CdRestrictClipRegion(struct dc* dc, struct region rgn);
struct region CdGetCopyOfClipRegion(struct dc* dc);
struct region CdIntersectWithClipRegion(struct dc* dc, struct region rgn);

#define CdIntersectRegion(a, b)   CdGetRegionCombination(REGION_COMBINE_INTERSECT, a, b)
#define CdUnionRegion(a, b)       CdGetRegionCombination(REGION_COMBINE_UNION, a, b)
#define CdSubtractRegion(a, b)    CdGetRegionCombination(REGION_COMBINE_DIFFERENCE, a, b)
#define CdXorRegion(a, b)         CdGetRegionCombination(REGION_COMBINE_XOR, a, b)

#define CdIntersectRegionInPlace(a, b)   CdGetRegionCombinationInPlace(REGION_COMBINE_INTERSECT, a, b)
#define CdUnionRegionInPlace(a, b)       CdGetRegionCombinationInPlace(REGION_COMBINE_UNION, a, b)
#define CdSubtractRegionInPlace(a, b)    CdGetRegionCombinationInPlace(REGION_COMBINE_DIFFERENCE, a, b)
#define CdXorRegionInPlace(a, b)         CdGetRegionCombinationInPlace(REGION_COMBINE_XOR, a, b)

struct pen* CdCreateSolidPen(colour_t colour, int thickness);
struct pen* CdCreatePatternedPen(colour_t colour, int thickness, int pattern);
struct pen* CdCreateCustomPen(colour_t colour, int width, int height, // maxheight=8
    uint8_t* rows_bitmap); 

colour_t CdGetPenColour(struct pen* pen);
int CdSetPenColour(struct pen* pen, colour_t col);
int CdGetPenPattern(struct pen* pen);
int CdSetPenPattern(struct pen* pen, int pattern);
int CdGetPenThickness(struct pen* pen);
int CdSetPenThickness(struct pen* pen, int thickness);
struct pen* CdGetStockPen(int type);
bool CdIsStockPen(struct pen* pen);
struct pen* CdCopyPen(struct pen* pen);
int CdSetPenOrigin(struct pen* pen, int x, int y);
struct point CdGetPenOrigin(struct pen* pen);


/* PAGEABLE - CAN'T BE USED INTERNALLY BUT HAS WRAPPER */
bool CdIsOverlappingRegion(struct region a, struct region b);
bool CdIsSubRegion(struct region super, struct region sub);
bool CdIsRegionEqual(struct region a, struct region b);
bool CdIsPointInRegion(struct region rgn, int x, int y);
int CdGetRegionCombinationInPlace(int mode, struct region* a, struct region b);

struct region CdCreateEllipseRegion(int x, int y, int width, int height);
struct region CdCreatePolyPolygonRegion(int* px, int* py, int* counts, int polygons, int mode);

int CdPaintRoundedRectWithBrush(struct dc* dc, int x, int y, int width, int height, 
    int radius, struct brush* brush);
int CdPaintRoundedRect(struct dc* dc, int x, int y, int width, int height, 
    int radius);
int CdPaintEllipseWithBrush(struct dc* dc, int x, int y, int width, int height, 
    struct brush* brush);
int CdPaintEllipse(struct dc* dc, int x, int y, int width, int height);
int CdPaintPolygonWithBrush(struct dc* dc, int* x, int* y, int points, int mode, 
    struct brush* brush);
int CdPaintPolygon(struct dc* dc, int* x, int* y, int points, int mode);
struct region CdCreateRoundedRectRegion(int x, int y, int width, int height, int radius);


struct path CdCreatePath(int start_x, int start_y);
struct region CdClosePath(struct path p, int mode);

void CdSetPathTransform(
    struct path* p, 
    int scale_x_16_16, int scale_y_16_16, 
    int trans_x, int trans_y
);

struct path CdCopyPath(struct path p);

void CdDrawPath(struct path* p, int x, int y);
void CdDrawCubicBeizerPath(struct path* p, int x, int y, int c1x, int c1y, int c2x, int c2y);
void CdDrawQuadraticBezierPath(struct path* p, int x, int y, int cx, int cy);

void CdLiftPath(struct path* p, int start_x, int start_y);
