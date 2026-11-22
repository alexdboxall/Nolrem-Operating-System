#include "clipdraw_internal.h"
#include "api.h"
#include <obj.h>
#include <errno.h>
#include <heap.h>
#include <string.h>

static void CleanupPen(void* _pn) { 
    struct pen* pn = _pn;
    if (pn == NULL) {
        return;
    }
    FreeHeap(pn);
}

// TODO: LOCKS!

export struct pen* CdCreateCustomPen(colour_t colour, int width, int height, uint8_t* rows_bitmap) {
    if (height < 1 || height > 8 || width < 1 || width > 8) {
        return NULL;
    }
    struct pen* pn = AllocHeap(sizeof(struct pen));
    if (pn == NULL) {
        return NULL;
    }
    KeInitUserObject(pn, UOBJ_PEN);
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
    KeLockUserObject(pen);
    memcpy(pn, pen, sizeof(struct pen));
    KeUnlockUserObject(pen);
    KeInitUserObject(pn, UOBJ_PEN);
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
    KeLockUserObject(pen);
    colour_t retv = pen->col;
    KeUnlockUserObject(pen);
    return retv;
}

export int CdSetPenColour(struct pen* pen, colour_t col) {
    KeLockUserObject(pen);
    pen->col = col;
    KeUnlockUserObject(pen);
    return 0;
}

export int CdGetPenPattern(struct pen* pen) {
    KeLockUserObject(pen);
    if (pen->is_custom) {
        KeUnlockUserObject(pen);
        return PEN_INVALID;
    }
    int retv = pen->pattern_type;
    KeUnlockUserObject(pen);
    return retv;
}

export int CdSetPenPattern(struct pen* pen, int pattern) {
    /* Check first, so a bad pattern can't leave the pen half-modified. */
    if (pattern < 0 || pattern > _HIGHEST_USED_PEN_TYPE) {
        return EINVAL;
    }
    KeLockUserObject(pen);
    pen->pattern_type = pattern;
    pen->is_custom = false;
    pen->pat_height = (pattern == PEN_PATTERN_SQUIGGLY || pattern == PEN_PATTERN_DOUBLE) ? 2 : 1;
    switch (pattern) {
    case PEN_PATTERN_SOLID:
    case PEN_PATTERN_DOUBLE:
        pen->pat_width = 8;
        pen->pattern[0] = 0b11111111;
        pen->pattern[1] = 0b11111111;
        KeUnlockUserObject(pen);
        return 0;

    case PEN_PATTERN_DASH:
        pen->pat_width = 8;
        pen->pattern[0] = 0b11101110;
        KeUnlockUserObject(pen);
        return 0;

    case PEN_PATTERN_DOT:
        pen->pat_width = 8;
        pen->pattern[0] = 0b10101010;
        KeUnlockUserObject(pen);
        return 0;

    case PEN_PATTERN_DASH_DOT:
        pen->pat_width = 6;
        pen->pattern[0] = 0b111010;
        KeUnlockUserObject(pen);
        return 0;

    case PEN_PATTERN_DASH_DOT_DOT:
        pen->pat_width = 8;
        pen->pattern[0] = 0b11101010;
        KeUnlockUserObject(pen);
        return 0;

    case PEN_PATTERN_SQUIGGLY:
        pen->pat_width = 8;
        pen->pattern[0] = 0b10101010;
        pen->pattern[1] = 0b01010101;
        KeUnlockUserObject(pen);
        return 0;

    case PEN_PATTERN_NONE:
        pen->pat_width = 8;
        pen->pattern[0] = 0;
        KeUnlockUserObject(pen);
        return 0;

    default:
        KeUnlockUserObject(pen);
        return EINVAL;
    }
}

export int CdGetPenThickness(struct pen* pen) {
    KeLockUserObject(pen);
    int retv = pen->thickness;
    KeUnlockUserObject(pen);
    return retv;
}

export int CdSetPenThickness(struct pen* pen, int thickness) {
    KeLockUserObject(pen);
    pen->thickness = thickness;
    KeUnlockUserObject(pen);
    return 0;
}

export int CdSetPenOrigin(struct pen* pen, int x, int y) {
    KeLockUserObject(pen);
    pen->origin_x = x & 7;
    pen->origin_y = y & 7;
    KeUnlockUserObject(pen);
    return 0;
}

export struct point CdGetPenOrigin(struct pen* pen) {
    KeLockUserObject(pen);
    struct point retv = (struct point) {
        .x = pen->origin_x,
        .y = pen->origin_y
    };
    KeUnlockUserObject(pen);
    return retv;
}


static struct pen* stock_pens[_NUM_STOCK_PENS];

export struct pen* CdGetStockPen(int type) {
    if (type >= 0 && type < _NUM_STOCK_PENS) {
        return stock_pens[type];
    } else {
        return NULL;
    }
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

    KeRegisterUserObjectType(UOBJ_PEN, CleanupPen);
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
}

