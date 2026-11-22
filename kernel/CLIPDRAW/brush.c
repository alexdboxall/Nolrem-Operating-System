#include "clipdraw_internal.h"
#include "api.h"
#include <obj.h>
#include <errno.h>
#include <heap.h>
#include <string.h>

#include "tables/brush_tables.h"

static void CdCleanupBrush(void* _br) { 
    struct brush* br = _br;
    if (br == NULL) {
        return;
    }
    FreeHeap(br);
}

static struct brush* CdCreateBrush(colour_t primary, colour_t secondary, uint8_t* pattern, int type) {
    struct brush* br = AllocHeap(sizeof(struct brush));
    if (br == NULL) {
        return NULL;
    }
    KeInitUserObject(br, UOBJ_BRUSH);
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
    if (br == NULL) {
        return NULL;
    }
    KeInitUserObject(br, UOBJ_BRUSH);
    KeLockUserObject(old_br);
    br->origin_x = old_br->origin_x;
    br->origin_y = old_br->origin_y;
    br->pattern_type = old_br->pattern_type;
    br->primary = old_br->primary;
    br->secondary = old_br->secondary;
    memcpy(br->pattern, old_br->pattern, sizeof(br->pattern));
    KeUnlockUserObject(old_br);
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
    KeLockUserObject(br);
    /* Can't change a custom brush to a standard pattern. */
    if (br->pattern_type == BRUSH_PATTERN_CUSTOM) {
        KeUnlockUserObject(br);
        return EINVAL;
    }
    br->pattern_type = pattern;
    memcpy(br->pattern, standard_brush_patterns[pattern], sizeof(br->pattern));
    KeUnlockUserObject(br);
    return 0;
}

export int CdGetBrushPattern(struct brush* br) {
    KeLockUserObject(br);
    int retv = br->pattern_type;
    KeUnlockUserObject(br);
    return retv;
}

export int CdSetBrushColour(struct brush* br, colour_t argb) {
    KeLockUserObject(br);
    br->primary = argb;
    KeUnlockUserObject(br);
    return 0;
}

export colour_t CdGetBrushColour(struct brush* br) {
    KeLockUserObject(br);
    colour_t retv = br->primary;
    KeUnlockUserObject(br);
    return retv;
}

export int CdSetBrushSecondaryColour(struct brush* br, colour_t argb) {
    KeLockUserObject(br);
    br->secondary = argb;
    KeUnlockUserObject(br);
    return 0;
}

export colour_t CdGetBrushSecondaryColour(struct brush* br) {
    KeLockUserObject(br);
    colour_t retv = br->secondary;
    KeUnlockUserObject(br);
    return retv;
}

export int CdSetBrushOrigin(struct brush* br, int x, int y) {
    KeLockUserObject(br);
    br->origin_x = x & 7;
    br->origin_y = y & 7;
    KeUnlockUserObject(br);
    return 0;
}

export struct point CdGetBrushOrigin(struct brush* br) {
    KeLockUserObject(br);
    struct point retv = (struct point) {
        .x = br->origin_x,
        .y = br->origin_y
    };
    KeUnlockUserObject(br);
    return retv;
}

static struct brush* stock_brushes[4];

export struct brush* CdGetStockBrush(int type) {
    if (type >= 0 && type < 4) {
        return stock_brushes[type];
    } else {
        return NULL;
    }
}

export bool CdIsStockBrush(struct brush* br) {
    for (int i = 0; i < 4; ++i) {
        if (stock_brushes[i] == br) {
            return true;
        }
    }
    return false;
}

void CdInitBrushSubsystem(void) {
    KeRegisterUserObjectType(UOBJ_BRUSH, CdCleanupBrush);
    stock_brushes[STOCK_BRUSH_BLACK] = CdCreateSolidBrush(BlackColour());
    stock_brushes[STOCK_BRUSH_WHITE] = CdCreateSolidBrush(WhiteColour());
    stock_brushes[STOCK_BRUSH_TRANSPARENT] = CdCreateSolidBrush(TransparentColour());
    stock_brushes[STOCK_BRUSH_SYSTEM] = CdCreateSolidBrush(SystemColour());
    for (int i = 0; i < 4; ++i) {
        if (stock_brushes[i] != NULL) {
            KeMarkUserObjectPermanent(stock_brushes[i]);
        }
    }
}