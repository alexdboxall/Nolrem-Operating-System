#include "clipdraw_internal.h"
#include "api.h"
#include <obj.h>
#include <errno.h>
#include <heap.h>
#include <string.h>

static void CdCleanupPen(void* _pn) { 
    struct pen* pn = _pn;
    if (pn == NULL) {
        return;
    }
    FreeHeap(pn);
}

export struct pen* CdCreateCustomPen(colour_t colour, int width, int height, uint8_t* rows_bitmap) {
    if (height < 1 || height > 8 || width < 1 || width > 8) {
        return NULL;
    }
    struct pen* pn = AllocHeap(sizeof(struct pen));
    if (pn == NULL) {
        return NULL;
    }
    KeInitObject(pn, OBJ_PEN);
    KeInitSpinlock(&pn->lock);
    pn->col = colour;
    pn->is_custom = true;
    pn->origin_x = 0;
    pn->origin_y = 0;
    pn->pat_height = height;
    pn->pat_width = width;
    pn->thickness = height;
    memcpy(pn->pattern, rows_bitmap, height);
    return pn;
}

export struct pen* CdCopyPen(struct pen* pen) {
    struct pen* pn = AllocHeap(sizeof(struct pen));
    if (pn == NULL) {
        return NULL;
    }
    KeAcquireSpinlock(&pen->lock);
    memcpy(pn, pen, sizeof(struct pen));
    KeReleaseSpinlock(&pen->lock);
    KeInitObject(pn, OBJ_PEN);      /* the copy has the original's header and lock too, so these start both again */
    KeInitSpinlock(&pn->lock);
    return pn;
}

export struct pen* CdCreatePatternedPen(colour_t colour, int thickness, int pattern) {
    if (pattern < 0 || pattern > _HIGHEST_USED_PEN_TYPE) {
        return NULL;
    }
    uint8_t dummy = 0;
    struct pen* pen = CdCreateCustomPen(colour, 1, 1, &dummy);
    if (pen == NULL) {
        return NULL;
    }
    CdSetPenPattern(pen, pattern);
    pen->thickness = thickness;
    return pen;
}

export struct pen* CdCreateSolidPen(colour_t colour, int thickness) {
    return CdCreatePatternedPen(colour, thickness, PEN_PATTERN_SOLID);
}

export colour_t CdGetPenColour(struct pen* pen) {
    KeAcquireSpinlock(&pen->lock);
    colour_t retv = pen->col;
    KeReleaseSpinlock(&pen->lock);
    return retv;
}

export int CdSetPenColour(struct pen* pen, colour_t col) {
    KeAcquireSpinlock(&pen->lock);
    pen->col = col;
    KeReleaseSpinlock(&pen->lock);
    return 0;
}

export int CdGetPenPattern(struct pen* pen) {
    KeAcquireSpinlock(&pen->lock);
    if (pen->is_custom) {
        KeReleaseSpinlock(&pen->lock);
        return PEN_INVALID;
    }
    int retv = pen->pattern_type;
    KeReleaseSpinlock(&pen->lock);
    return retv;
}

export int CdSetPenPattern(struct pen* pen, int pattern) {
    /* Check first, so a bad pattern can't leave the pen half-modified. */
    if (pattern < 0 || pattern > _HIGHEST_USED_PEN_TYPE) {
        return EINVAL;
    }
    KeAcquireSpinlock(&pen->lock);
    pen->pattern_type = pattern;
    pen->is_custom = false;
    pen->pat_height = (pattern == PEN_PATTERN_SQUIGGLY || pattern == PEN_PATTERN_DOUBLE) ? 2 : 1;
    switch (pattern) {
    case PEN_PATTERN_SOLID:
    case PEN_PATTERN_DOUBLE:
        pen->pat_width = 8;
        pen->pattern[0] = 0b11111111;
        pen->pattern[1] = 0b11111111;
        KeReleaseSpinlock(&pen->lock);
        return 0;

    case PEN_PATTERN_DASH:
        pen->pat_width = 8;
        pen->pattern[0] = 0b11101110;
        KeReleaseSpinlock(&pen->lock);
        return 0;

    case PEN_PATTERN_DOT:
        pen->pat_width = 8;
        pen->pattern[0] = 0b10101010;
        KeReleaseSpinlock(&pen->lock);
        return 0;

    case PEN_PATTERN_DASH_DOT:
        pen->pat_width = 6;
        pen->pattern[0] = 0b111010;
        KeReleaseSpinlock(&pen->lock);
        return 0;

    case PEN_PATTERN_DASH_DOT_DOT:
        pen->pat_width = 8;
        pen->pattern[0] = 0b11101010;
        KeReleaseSpinlock(&pen->lock);
        return 0;

    case PEN_PATTERN_SQUIGGLY:
        pen->pat_width = 8;
        pen->pattern[0] = 0b10101010;
        pen->pattern[1] = 0b01010101;
        KeReleaseSpinlock(&pen->lock);
        return 0;

    case PEN_PATTERN_NONE:
        pen->pat_width = 8;
        pen->pattern[0] = 0;
        KeReleaseSpinlock(&pen->lock);
        return 0;

    default:
        KeReleaseSpinlock(&pen->lock);
        return EINVAL;
    }
}

export int CdGetPenThickness(struct pen* pen) {
    KeAcquireSpinlock(&pen->lock);
    int retv = pen->thickness;
    KeReleaseSpinlock(&pen->lock);
    return retv;
}

export int CdSetPenThickness(struct pen* pen, int thickness) {
    KeAcquireSpinlock(&pen->lock);
    pen->thickness = thickness;
    KeReleaseSpinlock(&pen->lock);
    return 0;
}

export int CdSetPenOrigin(struct pen* pen, int x, int y) {
    KeAcquireSpinlock(&pen->lock);
    pen->origin_x = x & 7;
    pen->origin_y = y & 7;
    KeReleaseSpinlock(&pen->lock);
    return 0;
}

export struct point CdGetPenOrigin(struct pen* pen) {
    KeAcquireSpinlock(&pen->lock);
    struct point retv = (struct point) {
        .x = pen->origin_x,
        .y = pen->origin_y
    };
    KeReleaseSpinlock(&pen->lock);
    return retv;
}


static struct pen* stock_pens[_NUM_STOCK_PENS];
static struct user_obj_ref* stock_pen_handles[_NUM_STOCK_PENS];

export struct pen* CdGetStockPen(int type) {
    if (type >= 0 && type < _NUM_STOCK_PENS) {
        return stock_pens[type];
    } else {
        return NULL;
    }
}

export struct user_obj_ref* CdGetStockPenHandle(int type) {
    if (type >= 0 && type < _NUM_STOCK_PENS) {
        return stock_pen_handles[type];
    } else {
        return NULL;
    }
}

export struct user_obj_ref* CdFindStockPenHandle(struct pen* pen) {
    for (int i = 0; i < _NUM_STOCK_PENS; ++i) {
        if (stock_pens[i] == pen) {
            return stock_pen_handles[i];
        }
    }
    return NULL;
}

export bool CdIsStockPen(struct pen* pen) {
    for (int i = 0; i < _NUM_STOCK_PENS; ++i) {
        if (stock_pens[i] == pen) {
            return true;
        }
    }
    return false;
}

void CdInitPenSubsystem(void) {
    uint8_t double_pattern[3] = {0xFF, 0x00, 0xFF};

    KeRegisterObjectType(OBJ_PEN, CdCleanupPen);
    stock_pens[STOCK_PEN_BLACK_1] = CdCreateSolidPen(BlackColour(), 1);
    stock_pens[STOCK_PEN_BLACK_2] = CdCreateSolidPen(BlackColour(), 2);
    stock_pens[STOCK_PEN_BLACK_3] = CdCreateSolidPen(BlackColour(), 3);
    stock_pens[STOCK_PEN_WHITE_1] = CdCreateSolidPen(WhiteColour(), 1);
    stock_pens[STOCK_PEN_WHITE_2] = CdCreateSolidPen(WhiteColour(), 2);
    stock_pens[STOCK_PEN_WHITE_3] = CdCreateSolidPen(WhiteColour(), 3);
    stock_pens[STOCK_PEN_SYSTEM_1] = CdCreateSolidPen(SystemColour(), 1);
    stock_pens[STOCK_PEN_SYSTEM_2] = CdCreateSolidPen(SystemColour(), 2);
    stock_pens[STOCK_PEN_SYSTEM_3] = CdCreateSolidPen(SystemColour(), 3);
    stock_pens[STOCK_PEN_TRANSPARENT] = CdCreateSolidPen(TransparentColour(), 1);
    stock_pens[STOCK_PEN_SQUIGGLY] = CdCreatePatternedPen(BlackColour(), 1, PEN_PATTERN_SQUIGGLY);
    stock_pens[STOCK_PEN_DOUBLE] = CdCreateCustomPen(
        BlackColour(), 8, 3, double_pattern
    );
    for (int i = 0; i < _NUM_STOCK_PENS; ++i) {
        if (stock_pens[i] != NULL) {
            /* As for the stock brushes. */
            KeRefObject(stock_pens[i]);
            stock_pen_handles[i] = KeCreateUserRef(stock_pens[i]);
            if (stock_pen_handles[i] != NULL) {
                KeMarkUserRefPermanent(stock_pen_handles[i]);
            }
        }
    }
}

