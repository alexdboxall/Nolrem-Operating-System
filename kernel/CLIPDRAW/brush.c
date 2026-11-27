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
    KeInitObject(br, OBJ_BRUSH);
    KeInitSpinlock(&br->lock);
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
    KeInitObject(br, OBJ_BRUSH);
    KeInitSpinlock(&br->lock);
    KeAcquireSpinlock(&old_br->lock);
    br->origin_x = old_br->origin_x;
    br->origin_y = old_br->origin_y;
    br->pattern_type = old_br->pattern_type;
    br->primary = old_br->primary;
    br->secondary = old_br->secondary;
    memcpy(br->pattern, old_br->pattern, sizeof(br->pattern));
    KeReleaseSpinlock(&old_br->lock);
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
    KeAcquireSpinlock(&br->lock);
    /* Can't change a custom brush to a standard pattern. */
    if (br->pattern_type == BRUSH_PATTERN_CUSTOM) {
        KeReleaseSpinlock(&br->lock);
        return EINVAL;
    }
    br->pattern_type = pattern;
    memcpy(br->pattern, standard_brush_patterns[pattern], sizeof(br->pattern));
    KeReleaseSpinlock(&br->lock);
    return 0;
}

export int CdGetBrushPattern(struct brush* br) {
    KeAcquireSpinlock(&br->lock);
    int retv = br->pattern_type;
    KeReleaseSpinlock(&br->lock);
    return retv;
}

export int CdSetBrushColour(struct brush* br, colour_t argb) {
    KeAcquireSpinlock(&br->lock);
    br->primary = argb;
    KeReleaseSpinlock(&br->lock);
    return 0;
}

export colour_t CdGetBrushColour(struct brush* br) {
    KeAcquireSpinlock(&br->lock);
    colour_t retv = br->primary;
    KeReleaseSpinlock(&br->lock);
    return retv;
}

export int CdSetBrushSecondaryColour(struct brush* br, colour_t argb) {
    KeAcquireSpinlock(&br->lock);
    br->secondary = argb;
    KeReleaseSpinlock(&br->lock);
    return 0;
}

export colour_t CdGetBrushSecondaryColour(struct brush* br) {
    KeAcquireSpinlock(&br->lock);
    colour_t retv = br->secondary;
    KeReleaseSpinlock(&br->lock);
    return retv;
}

export int CdSetBrushOrigin(struct brush* br, int x, int y) {
    KeAcquireSpinlock(&br->lock);
    br->origin_x = x & 7;
    br->origin_y = y & 7;
    KeReleaseSpinlock(&br->lock);
    return 0;
}

export struct point CdGetBrushOrigin(struct brush* br) {
    KeAcquireSpinlock(&br->lock);
    struct point retv = (struct point) {
        .x = br->origin_x,
        .y = br->origin_y
    };
    KeReleaseSpinlock(&br->lock);
    return retv;
}

#define NUM_STOCK_BRUSHES   4

static struct brush* stock_brushes[NUM_STOCK_BRUSHES];
static struct user_obj_ref* stock_brush_handles[NUM_STOCK_BRUSHES];

export struct brush* CdGetStockBrush(int type) {
    if (type >= 0 && type < NUM_STOCK_BRUSHES) {
        return stock_brushes[type];
    } else {
        return NULL;
    }
}

export struct user_obj_ref* CdGetStockBrushHandle(int type) {
    if (type >= 0 && type < NUM_STOCK_BRUSHES) {
        return stock_brush_handles[type];
    } else {
        return NULL;
    }
}

export struct user_obj_ref* CdFindStockBrushHandle(struct brush* br) {
    for (int i = 0; i < NUM_STOCK_BRUSHES; ++i) {
        if (stock_brushes[i] == br) {
            return stock_brush_handles[i];
        }
    }
    return NULL;
}

export bool CdIsStockBrush(struct brush* br) {
    for (int i = 0; i < NUM_STOCK_BRUSHES; ++i) {
        if (stock_brushes[i] == br) {
            return true;
        }
    }
    return false;
}

void CdInitBrushSubsystem(void) {
    KeRegisterObjectType(OBJ_BRUSH, CdCleanupBrush);
    stock_brushes[STOCK_BRUSH_BLACK] = CdCreateSolidBrush(BlackColour());
    stock_brushes[STOCK_BRUSH_WHITE] = CdCreateSolidBrush(WhiteColour());
    stock_brushes[STOCK_BRUSH_TRANSPARENT] = CdCreateSolidBrush(TransparentColour());
    stock_brushes[STOCK_BRUSH_SYSTEM] = CdCreateSolidBrush(SystemColour());
    for (int i = 0; i < NUM_STOCK_BRUSHES; ++i) {
        if (stock_brushes[i] != NULL) {
            /* The handle is permanent, and the brush under it lives as long as the table does, so this is all
               forever. Make the handle's own reference first, as creating it uses one up. */
            KeRefObject(stock_brushes[i]);
            stock_brush_handles[i] = KeCreateUserRef(stock_brushes[i]);
            if (stock_brush_handles[i] != NULL) {
                KeMarkUserRefPermanent(stock_brush_handles[i]);
            }
        }
    }
}