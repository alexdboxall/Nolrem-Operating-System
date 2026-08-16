#include <common.h>
#include <syscall.h>
#include <errno.h>

static size_t (* const syscall_table[])(size_t, size_t, size_t, size_t) = {
    [SYS_CreateRectRegion] = SysCreateRectRegion,
    [SYS_Ref] = SysRef,
    [SYS_GetRegionCombination] = SysGetRegionCombination,
    [SYS_CreateEllipseRegion] = SysCreateEllipseRegion,
    [SYS_EmptyOrEveryRegion] = SysEmptyOrEveryRegion,
    [SYS_CreateRoundedRectRegion] = SysCreateRoundedRectRegion,
    [SYS_CreatePolyPolygonRegion] = SysCreatePolyPolygonRegion
};

export size_t PerformSystemCall(size_t call, size_t a, size_t b, size_t c, size_t d) {
    if (call >= sizeof(syscall_table) / sizeof(syscall_table[0])) {
        return ENOSYS;
    }
    if (syscall_table[call] == NULL) {
        return ENOSYS;
    }
    return syscall_table[call](a, b, c, d);
}