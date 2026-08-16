#include "clipdraw_internal.h"
#include "api.h"
#include <obj.h>
#include <errno.h>
#include <heap.h>
#include <string.h>

#include "tables/brush_tables.h"

static void CleanupBrush(void* _br) { 
    struct brush* br = _br;
    FreeHeap(br);
}

static struct brush* CdCreateBrush(colour_t primary, colour_t secondary, uint8_t* pattern, int type) {
    struct brush* br = AllocHeap(sizeof(struct brush));
    InitUserObject(br, UOBJ_BRUSH);
    br->origin_x = 0;
    br->origin_y = 0;
    br->pattern_type = type;
    br->primary = primary;
    br->secondary = secondary;
    memcpy(br->pattern, pattern, sizeof(br->pattern));
    return br;
}

export struct brush* CdCopyBrush(struct brush* old_br) {
    struct brush* br = AllocHeap(sizeof(struct brush));
    InitUserObject(br, UOBJ_BRUSH);
    br->origin_x = old_br->origin_x;
    br->origin_y = old_br->origin_y;
    br->pattern_type = old_br->pattern_type;
    br->primary = old_br->primary;
    br->secondary = old_br->secondary;
    memcpy(br->pattern, old_br->pattern, sizeof(br->pattern));
    return br;
}

export struct brush* CdCreatePatternedBrush(colour_t primary, colour_t secondary, int pattern) {
    if (pattern < 0 || pattern >= BRUSH_PATTERN_CUSTOM) {
        return NULL;
    }
    return CdCreateBrush(primary, secondary, standard_brush_patterns[pattern], (uint8_t) pattern);
}

export struct brush* CdCreateSolidBrush(colour_t argb) {
    return CdCreatePatternedBrush(argb, argb, BRUSH_PATTERN_SOLID);
}

export struct brush* CdCreateCustomBrush(colour_t primary, colour_t secondary, uint8_t* pattern) {
    return CdCreateBrush(primary, secondary, pattern, BRUSH_PATTERN_CUSTOM);
}

export int CdSetBrushPattern(struct brush* br, int pattern) {
    if (pattern < 0 || pattern >= BRUSH_PATTERN_CUSTOM) {
        return EINVAL;
    }
    br->pattern_type = br->pattern_type;
    memcpy(br->pattern, standard_brush_patterns[pattern], sizeof(br->pattern));
    return 0;
}

export int CdGetBrushPattern(struct brush* br) {
    return br->pattern_type;
}

export int CdSetBrushColour(struct brush* br, colour_t argb) {
    br->primary = argb;
    return 0;
}

export colour_t CdGetBrushColour(struct brush* br) {
    return br->primary;
}

export int CdSetBrushSecondaryColour(struct brush* br, colour_t argb) {
    br->secondary = argb;
    return 0;
}

export colour_t CdGetBrushSecondaryColour(struct brush* br) {
    return br->secondary;
}

export int CdSetBrushOrigin(struct brush* br, int x, int y) {
    br->origin_x = x & 7;
    br->origin_y = y & 7;
    return 0;
}

export struct point CdGetBrushOrigin(struct brush* br) {
    return (struct point) {
        .x = br->origin_x,
        .y = br->origin_y
    };
}

static struct brush* stock_brushes[4];

export struct brush* CdGetStockBrush(int type) {
    if (type >= 0 && type < 4) {
        return stock_brushes[type];
    } else {
        return NULL;
    }
}

void CdInitBrushSubsystem(void) {
    RegisterUserObjectType(UOBJ_BRUSH, CleanupBrush);
    stock_brushes[STOCK_BRUSH_BLACK] = CdCreateSolidBrush(BlackColour());
    stock_brushes[STOCK_BRUSH_WHITE] = CdCreateSolidBrush(WhiteColour());
    stock_brushes[STOCK_BRUSH_TRANSPARENT] = CdCreateSolidBrush(TransparentColour());
    stock_brushes[STOCK_BRUSH_SYSTEM] = CdCreateSolidBrush(SystemColour());
}