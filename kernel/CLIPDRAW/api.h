#pragma once

#include <stddef.h>
#include <stdint.h>
#include <common.h>

#include <keycodes.h>

struct region {
    void* data;
    size_t used_length;     
    size_t allocated_length;
};

struct uregion;

typedef struct uregion* region_t;

struct point {
    int x;
    int y;
};

struct size {
    int width;
    int height;
};

struct rect {
    int x;
    int y;
    int w;
    int h;
};


/* Object Functions */
void Ref(void*);
void Deref(void*);

/* Helper Functions */
int IntegerSqrt(int x);

/* Region Functions */

#define REGION_COMBINE_INTERSECT    0
#define REGION_COMBINE_UNION        1
#define REGION_COMBINE_DIFFERENCE   2
#define REGION_COMBINE_XOR          3

// all functions returning a region require you to free them manually
// INCLUDING EmptyRegion and EverythingRegion

#define POLYGON_ALTERNATE           0
#define POLYGON_WINDING             1

region_t CreateRectRegion(int x, int y, int width, int height);
region_t CreateRoundedRectRegion(int x, int y, int width, int height, 
    int radius);
region_t CreateEllipseRegion(int x, int y, int width, int height);
region_t CreatePolygonRegion(int* x, int* y, int points, int mode);
region_t CreatePolyPolygonRegion(int* x, int* y, int* counts, int polygons, int mode);

region_t EmptyRegion(void);
region_t EverythingRegion(void);

region_t GetRegionCombination(int mode, region_t a, region_t b);
void GetRegionCombinationInPlace(int mode, region_t* a, region_t b);

// helpers
#define IntersectRegion(a, b)   GetRegionCombination(REGION_COMBINE_INTERSECT, a, b)
#define UnionRegion(a, b)       GetRegionCombination(REGION_COMBINE_UNION, a, b)
#define SubtractRegion(a, b)    GetRegionCombination(REGION_COMBINE_DIFFERENCE, a, b)
#define XorRegion(a, b)         GetRegionCombination(REGION_COMBINE_XOR, a, b)

#define IntersectRegionInPlace(a, b)   GetRegionCombinationInPlace(REGION_COMBINE_INTERSECT, a, b)
#define UnionRegionInPlace(a, b)       GetRegionCombinationInPlace(REGION_COMBINE_UNION, a, b)
#define SubtractRegionInPlace(a, b)    GetRegionCombinationInPlace(REGION_COMBINE_DIFFERENCE, a, b)
#define XorRegionInPlace(a, b)         GetRegionCombinationInPlace(REGION_COMBINE_XOR, a, b)

int TranslateRegion(region_t* rgn, int offx, int offy);

region_t CopyRegion(region_t rgn);

bool IsRegionEqual(region_t a, region_t b);
bool IsRegionEmpty(region_t rgn);
struct rect GetRegionBounds(region_t rgn);

bool IsPointInRegion(region_t rgn, int x, int y);
bool IsSubRegion(region_t super, region_t sub);
bool IsOverlappingRegion(region_t a, region_t b);

/* 
 * not a quick operation! calls GetRegionCombination!
 * shuffles the whole thing such that trans_x and trans_y are 0
 */
region_t ResetRegionOrigin(region_t rgn);


/* Colour Functions */

typedef uint32_t colour_t;

static inline colour_t CreateAlphaColour(uint8_t red, uint8_t green, uint8_t blue, uint8_t alpha) {
    uint32_t col = alpha;
    col <<= 8;
    col |= red; 
    col <<= 8;
    col |= green;
    col <<= 8;
    col |= blue;
    return col;
}

static inline colour_t CreateColour(uint8_t red, uint8_t green, uint8_t blue) {
    return CreateAlphaColour(red, green, blue, 0xFF);
}

static inline  uint8_t GetRed(colour_t argb) {
    return (argb >> 16) & 0xFF;
}

static inline  uint8_t GetGreen(colour_t argb) {
    return (argb >> 8) & 0xFF;
}

static inline  uint8_t GetBlue(colour_t argb) {
    return (argb >> 0) & 0xFF;
}

static inline  uint8_t GetAlpha(colour_t argb) {
    return (argb >> 24) & 0xFF;
}

static inline colour_t TransparentColour(void) {
    return 0;
}

static inline bool IsOpaqueColour(colour_t argb) {
    return GetAlpha(argb) == 0xFF;
}

#define BlackColour() 0xFF000000
#define WhiteColour() 0xFFFFFFFF
colour_t TransparentColour(void);
colour_t SystemColour(void);

bool IsOpaqueColour(colour_t argb);



/* Painting Functions */

#define FLOOD_FILL_SURFACE  0
#define FLOOD_FILL_BORDER   1

struct dc;
struct pen;
struct brush;
struct font;

typedef struct dc* dc_t;
typedef struct pen* pen_t;
typedef struct brush* brush_t;
typedef struct font* font_t;

// both the bitmap DC's clip region and the real DC clipregion are used
dc_t PaintBitmap(dc_t dc, dc_t bitmap, int x, int y);

int PaintTextWithFont(dc_t dc, const char* text, int x, int y, font_t font);
int PaintText(dc_t dc, const char* text, int x, int y);
int GreyText(dc_t dc, const char* text, int x, int y);

int PaintLineWithPen(dc_t dc, int x1, int y1, int x2, int y2, pen_t pen);
int PaintLine(dc_t dc, int x1, int y1, int x2, int y2);

int PaintRectWithBrush(dc_t dc, int x, int y, int width, int height, 
    brush_t brush);
int PaintRect(dc_t dc, int x, int y, int width, int height);

int PaintRoundedRectWithBrush(dc_t dc, int x, int y, int width, int height, 
    int radius, brush_t brush);
int PaintRoundedRect(dc_t dc, int x, int y, int width, int height, 
    int radius);

int PaintEllipseWithBrush(dc_t dc, int x, int y, int width, int height, 
    brush_t brush);
int PaintEllipse(dc_t dc, int x, int y, int width, int height);

int PaintPolygonWithBrush(dc_t dc, int* x, int* y, int points, int mode, 
    brush_t brush);
int PaintPolygon(dc_t dc, int* x, int* y, int points, int mode);

    
int PaintRegionWithBrush(dc_t dc, region_t rgn, brush_t brush);
int PaintRegion(dc_t dc, region_t rgn);

int InvertRect(dc_t dc, int x, int y, int width, int height);
int InvertRegion(dc_t dc, region_t rgn);

int FloodFill(dc_t dc, int x, int y, colour_t col, int mode);

int FrameRectWithPen(dc_t dc, int x, int y, int w, int h, pen_t pen);
int FrameRect(dc_t dc, int x, int y, int w, int h);

int FrameRegionWithPen(dc_t dc, region_t rgn, pen_t pen);
int FrameRegion(dc_t dc, region_t rgn);





/* Brush Functions */
#define BRUSH_INVALID                   (-1)

#define BRUSH_PATTERN_SOLID             0
#define BRUSH_PATTERN_50_PERCENT        1
#define BRUSH_PATTERN_25_PERCENT        2
#define BRUSH_PATTERN_75_PERCENT        3
#define BRUSH_PATTERN_DIAG_FWD          4
#define BRUSH_PATTERN_DIAG_BACK         5
#define BRUSH_PATTERN_CROSS             6
#define BRUSH_PATTERN_DIAG_CROSS        7
#define BRUSH_PATTERN_HZ                8
#define BRUSH_PATTERN_VT                9
#define BRUSH_PATTERN_NONE              10
#define BRUSH_PATTERN_BIG_CROSS         11
#define BRUSH_PATTERN_BIG_DIAG_CROSS    12
#define BRUSH_PATTERN_HZ_THICK          13
#define BRUSH_PATTERN_VT_THICK          14
#define BRUSH_PATTERN_HZ_THIN           15
#define BRUSH_PATTERN_VT_THIN           16
#define BRUSH_PATTERN_HZ_DOUBLE         17
#define BRUSH_PATTERN_VT_DOUBLE         18
#define BRUSH_PATTERN_BIG_DIAG_FWD      19
#define BRUSH_PATTERN_BIG_DIAG_BACK     20
#define BRUSH_PATTERN_12_5_PERCENT      21
#define BRUSH_PATTERN_87_5_PERCENT      22
#define BRUSH_PATTERN_CUSTOM            23

brush_t CreateSolidBrush(colour_t argb);
brush_t CreatePatternedBrush(colour_t primary, colour_t secondary, int pattern);

// 8x8 dot pattern
brush_t CreateCustomBrush(colour_t primary, colour_t secondary, uint8_t* pattern);

// can't use on custom brush, also can't change to custom brush
int SetBrushPattern(brush_t br, int pattern);
int GetBrushPattern(brush_t br);

int SetBrushColour(brush_t br, colour_t argb);
colour_t GetBrushColour(brush_t br);

int SetBrushSecondaryColour(brush_t br, colour_t argb);
colour_t GetBrushSecondaryColour(brush_t br);

#define STOCK_BRUSH_BLACK           0
#define STOCK_BRUSH_WHITE           1
#define STOCK_BRUSH_TRANSPARENT     2
#define STOCK_BRUSH_SYSTEM          3

// you do *NOT* need to close this handle - the stock brushes always have a 
// reference to them, so this func doesn't allocate you a new handle, so no
// need to free it. the stock brushes are used *to* avoid allocating and freeing
// new ones often
brush_t GetStockBrush(int type);

int SetBrushOrigin(brush_t br, int x, int y);
struct point GetBrushOrigin(brush_t br);

// unlike DuplicateHandle, makes a new brush object itself with duplicate data
// e.g. so you can take a stock brush and change its colour
brush_t CopyBrush(brush_t br);