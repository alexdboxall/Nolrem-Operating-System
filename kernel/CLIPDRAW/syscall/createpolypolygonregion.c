#include <common.h>
#include <obj.h>
#include <string.h>
#include <transfer.h>
#include <syscall.h>
#include "../api.h"
#include "../clipdraw_internal.h"
#include <limits.h>

struct polypoly {
    int* x;
    int* y;
    int* counts;
};

#define MAX_POLYGONS    50
#define MAX_POINTS      50000

pageable size_t SysCreatePolyPolygonRegion(size_t ppp, size_t, size_t polygons, size_t mode) {
    if (IsOutOfMemory()) {
        KeSetLastUserError(ENOMEM);
        return 0;
    }
    if (polygons < 1 || polygons > MAX_POLYGONS) {
        KeSetLastUserError(EINVAL);
        return 0;
    }

    struct polypoly pp;
    struct transfer tr = CreateTransferReadingFromUser((const void*) ppp, sizeof(struct polypoly), 0);
    
    int res = PerformTransfer(&pp, &tr, sizeof(struct polypoly));
    if (res != 0) {
        KeSetLastUserError(res);
        return res;
    }

    if (polygons > SIZE_MAX / sizeof(int)) {
        KeSetLastUserError(EINVAL);
        return 0;
    }

    int* cc = AllocHeap(sizeof(int) * polygons);
    if (!cc) {
        KeSetLastUserError(ENOMEM);
        return 0;
    }

    tr = CreateTransferReadingFromUser((const void*) pp.counts, sizeof(int) * polygons, 0);
    res = PerformTransfer(cc, &tr, sizeof(int) * polygons);
    if (res != 0) {
        FreeHeap(cc);
        KeSetLastUserError(res);
        return 0;
    }

    int calculated_sum = 0;
    for (size_t i = 0; i < polygons; ++i) {
        if (calculated_sum > MAX_POINTS - cc[i]) {
            KeSetLastUserError(EINVAL);
            FreeHeap(cc);
            return 0;
        }
        calculated_sum += cc[i];
    }
    if (calculated_sum == 0 || calculated_sum > MAX_POINTS) {
        FreeHeap(cc);
        KeSetLastUserError(EINVAL);
        return 0;
    }

    int* xx = AllocHeap(sizeof(int) * calculated_sum);
    int* yy = AllocHeap(sizeof(int) * calculated_sum);
    if (!xx || !yy) {
        if (xx) FreeHeap(xx);
        if (yy) FreeHeap(yy);
        FreeHeap(cc);
        KeSetLastUserError(ENOMEM);
        return 0;
    }

    tr = CreateTransferReadingFromUser((const void*) pp.x, sizeof(int) * calculated_sum, 0);
    if (PerformTransfer(xx, &tr, sizeof(int) * calculated_sum) != 0) {
        goto cleanup;
    }

    tr = CreateTransferReadingFromUser((const void*) pp.y, sizeof(int) * calculated_sum, 0);
    if (PerformTransfer(yy, &tr, sizeof(int) * calculated_sum) != 0) {
        goto cleanup;
    }

    struct region rgn = CdCreatePolyPolygonRegion(xx, yy, cc, polygons, mode);

    FreeHeap(xx);
    FreeHeap(yy);
    FreeHeap(cc);
    return (size_t) RegionToUserRegion(rgn);

cleanup:
    FreeHeap(xx);
    FreeHeap(yy);
    FreeHeap(cc);
    KeSetLastUserError(EINVAL);
    return 0;
}


export pageableuserexec region_t CreatePolyPolygonRegion(int* x, int* y, int* counts, int polygons, int mode) {
    struct polypoly pp;
    pp.x = x;
    pp.y = y;
    pp.counts = counts;
    size_t ret = SystemCall(SYS_CreatePolyPolygonRegion, (size_t) &pp, 0, polygons, mode);
    return (region_t) ret;
}

export pageableuserexec region_t CreatePolygonRegion(int* px, int* py, int points, int mode) {
    int count = points;
    return CreatePolyPolygonRegion(px, py, &count, 1, mode);
}
