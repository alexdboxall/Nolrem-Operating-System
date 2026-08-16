
#include <common.h>
#include <obj.h>
#include <string.h>
#include <syscall.h>
#include "../api.h"
#include "../clipdraw_internal.h"

export pageableuserexec bool IsOverlappingRegion(region_t a, region_t b) {
    region_t intersection = IntersectRegion(a, b);
    bool overlaps = !IsRegionEmpty(intersection);
    Deref(intersection);
    return overlaps;
}

export pageableuserexec bool IsSubRegion(region_t super, region_t sub) {
    region_t combined = UnionRegion(super, sub);
    bool is_sub = IsRegionEqual(combined, super);
    Deref(combined);
    return is_sub;
}