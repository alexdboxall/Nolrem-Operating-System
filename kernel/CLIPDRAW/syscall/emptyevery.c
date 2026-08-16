#include <common.h>
#include <obj.h>
#include <string.h>
#include <syscall.h>
#include "../api.h"
#include "../clipdraw_internal.h"

#define TYPE_EMPTY  0
#define TYPE_EVERY  1

pageable size_t SysEmptyOrEveryRegion(size_t type, size_t, size_t, size_t) {
    if (type == TYPE_EMPTY) {
        return (size_t) RegionToUserRegion(CdEmptyRegion());
    } else if (type == TYPE_EVERY) {
        return (size_t) RegionToUserRegion(CdEverythingRegion());
    } else {
        return 0;
    }
}

export pageableuserexec region_t EmptyRegion(void) {
    size_t ret = SystemCall(SYS_EmptyOrEveryRegion, TYPE_EMPTY, 0, 0, 0);
    return (region_t) ret;
}

export pageableuserexec region_t EverythingRegion(void) {
    size_t ret = SystemCall(SYS_EmptyOrEveryRegion, TYPE_EVERY, 0, 0, 0);
    return (region_t) ret;
}
