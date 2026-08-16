#include <common.h>
#include <obj.h>
#include <string.h>
#include <transfer.h>
#include <syscall.h>
#include "../api.h"
#include "../clipdraw_internal.h"

struct polypoly {
    int* x;
    int* y;
    int* counts;
    int count_sum;
};

pageable size_t SysCreatePolyPolygonRegion(size_t ppp, size_t, size_t polygons, size_t mode) {
    struct polypoly pp;
    struct transfer tr = CreateTransferReadingFromUser((const void*) ppp, sizeof(struct polypoly), 0);
    int res = PerformTransfer(&pp, &tr, sizeof(struct polypoly));
    if (res != 0) {
        return 0;
    }

    // TODO: validation!
    int* xx = AllocHeap(sizeof(int) * pp.count_sum);
    int* yy = AllocHeap(sizeof(int) * pp.count_sum);
    int* cc = AllocHeap(sizeof(int) * polygons);

    tr = CreateTransferReadingFromUser((const void*) pp.x, sizeof(int) * pp.count_sum, 0);
    res = PerformTransfer(&xx, &tr, sizeof(int) * pp.count_sum);
    if (res != 0) {
        FreeHeap(xx);
        FreeHeap(yy);
        FreeHeap(cc);
        return 0;
    }
    tr = CreateTransferReadingFromUser((const void*) pp.y, sizeof(int) * pp.count_sum, 0);
    res = PerformTransfer(&yy, &tr, sizeof(int) * pp.count_sum);
    if (res != 0) {
        FreeHeap(xx);
        FreeHeap(yy);
        FreeHeap(cc);
        return 0;
    }
    tr = CreateTransferReadingFromUser((const void*) pp.counts, sizeof(int) * polygons, 0);
    res = PerformTransfer(&cc, &tr, sizeof(int) * polygons);
    if (res != 0) {
        FreeHeap(xx);
        FreeHeap(yy);
        FreeHeap(cc);
        return 0;
    }

    struct region rgn = CdCreatePolyPolygonRegion(xx, yy, cc, polygons, mode);
    FreeHeap(xx);
    FreeHeap(yy);
    FreeHeap(cc);
    return (size_t) RegionToUserRegion(rgn);
}

export pageableuserexec region_t CreatePolyPolygonRegion(int* x, int* y, int* counts, int polygons, int mode) {
    struct polypoly pp;
    pp.x = x;
    pp.y = y;
    pp.counts = counts;
    int counts_sum = 0;
    for (int i = 0; i < polygons; ++i) {
        counts_sum += counts[i];
    }
    pp.count_sum = counts_sum;
    size_t ret = SystemCall(SYS_CreatePolyPolygonRegion, (size_t) &pp, 0, polygons, mode);
    return (region_t) ret;
}

export pageableuserexec region_t CreatePolygonRegion(int* px, int* py, int points, int mode) {
    int count = points;
    return CreatePolyPolygonRegion(px, py, &count, 1, mode);
}
