#include "../clipdraw_internal.h"
#include "../region_internal.h"
#include <string.h>

export pageable bool CdIsOverlappingRegion(struct region a, struct region b) {
    struct region intersection = CdIntersectRegion(a, b);
    bool overlaps = !CdIsRegionEmpty(intersection);
    CdFreeRegion(intersection);
    return overlaps;
}

export pageable bool CdIsSubRegion(struct region super, struct region sub) {
    struct region combined = CdUnionRegion(super, sub);
    bool is_sub = CdIsRegionEqual(combined, super);
    CdFreeRegion(combined);
    return is_sub;
}

static pageable struct region CdResetRegionOrigin(struct region rgn) {
    /*
     * Keep the region logically the same, but 'bake in' the translation
     * so that trans_x and trans_y are both (0, 0).
     *
     * The easiest way is to just use GetRegionCombination to take a union with 
     * an empty region, as GetRegionCombination always spits out something with
     * trans_x and trans_y as (0, 0).
     */

    struct region empty = CdEmptyRegion();
    struct region normalized = CdUnionRegion(rgn, empty);
    CdFreeRegion(empty);
    return normalized;
}

export pageable bool CdIsRegionEqual(struct region a, struct region b) {
    if (a.used_length != b.used_length) {
        return false;
    }

    struct region_data* a_data = a.data;
    struct region_data* b_data = b.data;

    if (a_data->num_bands != b_data->num_bands) {
        return false;
    }

    if (a_data->trans_x == b_data->trans_x && a_data->trans_y == b_data->trans_y) {
        return !memcmp(a_data->band_data, b_data->band_data, a.used_length);
    }

    /* 
     * We only need to reset one of the regions the slow way. We just translate
     * normally such that one of them is at (0, 0) and then reset the other.
     * 
     * This modifies the regions we passed in! We have to revert it later!
     */
    int16_t a_trans_x = a_data->trans_x;
    int16_t a_trans_y = a_data->trans_y;
    a_data->trans_x = 0;
    a_data->trans_y = 0;
    b_data->trans_x -= a_trans_x;
    b_data->trans_y -= a_trans_y;
    
    bool equal;

    bool b_needs_reset = b_data->trans_x != 0 || b_data->trans_y != 0;
    if (b_needs_reset) {
        struct region reset_b = CdResetRegionOrigin(b);
        equal = CdIsRegionEqual(a, reset_b);
        CdFreeRegion(reset_b);
    
    } else {
        equal = CdIsRegionEqual(a, b);
    }

    /*
     * Revert our modifications to the region! 
     */
    a_data->trans_x = a_trans_x;
    a_data->trans_y = a_trans_y;
    b_data->trans_x += a_trans_x;
    b_data->trans_y += a_trans_y;

    return equal;
}

static pageable int PointInRectCallback(struct rect r, void* context, int rv, bool* cancel) {
    (void) rv;
    struct point* target = (struct point*) context;

    if (target->x >= r.x && 
        target->x < (r.x + r.w) &&
        target->y >= r.y && 
        target->y < (r.y + r.h)) {
        
        *cancel = true;
        return true;
    }

    return false;
}

export pageable bool CdIsPointInRegion(struct region rgn, int x, int y) {
    struct point target = { x, y };
    return IterateRegion(rgn, PointInRectCallback, &target, false);
}

export pageable void CdGetRegionCombinationInPlace(int mode, struct region* a, struct region b) {
    struct region c = CdGetRegionCombination(mode, *a, b);
    CdFreeRegion(*a);
    *a = c;
}
