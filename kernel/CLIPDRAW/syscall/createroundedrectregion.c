#include <common.h>
#include <obj.h>
#include <string.h>
#include <transfer.h>
#include <syscall.h>
#include "../api.h"
#include "../clipdraw_internal.h"

pageable size_t SysCreateRoundedRectRegion(size_t rect_ptr, size_t radius, size_t, size_t) {
    struct rect rect;
    struct transfer tr = CreateTransferReadingFromUser((const void*) rect_ptr, sizeof(struct rect), 0);
    int res = PerformTransfer(&rect, &tr, sizeof(struct rect));
    if (res != 0) {
        return 0;
    }
    int x = rect.x;
    int y = rect.y;
    int w = rect.w;
    int h = rect.h;
    struct region rgn = CdCreateRoundedRectRegion(x, y, w, h, radius);
    return (size_t) RegionToUserRegion(rgn);
}

export pageableuserexec region_t CreateRoundedRectRegion(int x, int y, int w, int h, int radius) {
    struct rect r;
    r.x = x;
    r.y = y;
    r.w = w;
    r.h = h;
    size_t ret = SystemCall(SYS_CreateRoundedRectRegion, (size_t) &r, radius, 0, 0);
    return (region_t) ret;
}
