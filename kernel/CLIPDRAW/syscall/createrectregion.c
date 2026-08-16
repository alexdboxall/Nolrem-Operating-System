#include <common.h>
#include <obj.h>
#include <string.h>
#include <syscall.h>
#include "../api.h"
#include "../clipdraw_internal.h"

pageable size_t SysCreateRectRegion(size_t a, size_t b, size_t c, size_t d) {
    int x = a;
    int y = b;
    int w = c;
    int h = d;
    struct region rgn = CdCreateRectRegion(x, y, w, h);
    return (size_t) RegionToUserRegion(rgn);
}

export pageableuserexec region_t CreateRectRegion(int x, int y, int width, int height) {
    size_t ret = SystemCall(SYS_CreateRectRegion, x, y, width, height);
    return (region_t) ret;
}
