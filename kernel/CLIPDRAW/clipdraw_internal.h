#include "api.h"
#include <obj.h>

#define UOBJ_INVALID             0
#define UOBJ_MUTEX               1
#define UOBJ_THREAD              2
#define UOBJ_PROCESS             3
#define UOBJ_FILE                4
#define UOBJ_DIRECTORY           5
#define UOBJ_TIMER               6
#define UOBJ_MEMORY_REGION       7
#define UOBJ_SOUND               8
#define UOBJ_DYNAMIC_LIBRARY     9
#define UOBJ_WINDOW              10
#define UOBJ_WINDOW_CLASS        11
#define UOBJ_BRUSH               12
#define UOBJ_PEN                 13
#define UOBJ_BITMAP              14
#define UOBJ_TYPEFACE            15
#define UOBJ_FONT                16
#define UOBJ_MAILBOX             17
#define UOBJ_REGION              18
#define UOBJ_DC                  19

/* INTERNAL ONLY */

struct graphics_driver;
void AddGraphicsFallbacksWhereNeeded(struct graphics_driver* drv);

struct region_iteration_context;

int IterateRegionCoroutine(
    struct region rgn,
    int retv,
    void* context,
    struct region_iteration_context* ctxt,
    bool (*band_callback)(int y0, int y1, int* retv, void* context),                        /* return TRUE to stop early */
    bool (*rect_callback)(int y0, int y1, int x0, int x1, int* retv, void* context)         /* as above */
);

int IterateRegion(
    struct region rgn, 
    int (*rect_callback)(struct rect r, void* context, int rv, bool* cancel), 
    void* context,
    int init_rv
);

struct graphics_driver* GetOutputDriver(struct dc*);

void CdInitBrushSubsystem(void);
void CdInitUserRegionSubsystem(void);

struct uregion {
    struct user_obj_header hdr;
    struct region rgn;
};

struct uregion* RegionToUserRegion(struct region rgn);
struct region UserRegionToRegion(struct uregion* urgn);

bool ValidateUserObjectAndAtomicallyRef(void* obj);
bool ValidateUserRegionAndAtomicallyRef(struct uregion* ur);

struct brush {
    struct user_obj_header hdr;
    colour_t primary;
    colour_t secondary;
    uint8_t pattern[8];
    uint8_t origin_x;
    uint8_t origin_y;
    uint8_t pattern_type;
};

/* INTERNAL, BUT HAS USER-WRAPPER*/
struct dc* CdCreateDc(void);
struct brush* CdGetDcBrush(struct dc* dc);

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

int CdSetBrushOrigin(struct brush* br, int x, int y);
struct point CdGetBrushOrigin(struct brush* br);

struct brush* CdCopyBrush(struct brush* br);

#define CdIntersectRegion(a, b)   CdGetRegionCombination(REGION_COMBINE_INTERSECT, a, b)
#define CdUnionRegion(a, b)       CdGetRegionCombination(REGION_COMBINE_UNION, a, b)
#define CdSubtractRegion(a, b)    CdGetRegionCombination(REGION_COMBINE_DIFFERENCE, a, b)
#define CdXorRegion(a, b)         CdGetRegionCombination(REGION_COMBINE_XOR, a, b)

#define CdIntersectRegionInPlace(a, b)   CdGetRegionCombinationInPlace(REGION_COMBINE_INTERSECT, a, b)
#define CdUnionRegionInPlace(a, b)       CdGetRegionCombinationInPlace(REGION_COMBINE_UNION, a, b)
#define CdSubtractRegionInPlace(a, b)    CdGetRegionCombinationInPlace(REGION_COMBINE_DIFFERENCE, a, b)
#define CdXorRegionInPlace(a, b)         CdGetRegionCombinationInPlace(REGION_COMBINE_XOR, a, b)



/* PAGEABLE - CAN'T BE USED INTERNALLY BUT HAS WRAPPER */
bool CdIsOverlappingRegion(struct region a, struct region b);
bool CdIsSubRegion(struct region super, struct region sub);
struct region CdResetRegionOrigin(struct region rgn);
bool CdIsRegionEqual(struct region a, struct region b);
bool CdIsPointInRegion(struct region rgn, int x, int y);
void CdGetRegionCombinationInPlace(int mode, struct region* a, struct region b);

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