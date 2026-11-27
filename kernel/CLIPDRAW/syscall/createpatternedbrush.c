#include <common.h>
#include <obj.h>
#include <string.h>
#include <syscall.h>
#include "../api.h"
#include "../clipdraw_internal.h"

pageable size_t SysCreatePatternedBrush(size_t primary, size_t secondary, size_t pattern, size_t) {
    if (KeIsOutOfMemory()) {
        KeSetLastUserError(ENOMEM);
        return 0;
    }
    /* Custom patterns need the pattern data, so go through SysCreateCustomBrush.
       This also rejects negative patterns, as they become huge unsigned values. */
    if (pattern >= BRUSH_PATTERN_CUSTOM) {
        KeSetLastUserError(EINVAL);
        return 0;
    }

    struct brush* br = CdCreatePatternedBrush((colour_t) primary, (colour_t) secondary, (int) pattern);
    if (br == NULL) {
        KeSetLastUserError(ENOMEM);
        return 0;
    }
    return (size_t) KeCreateUserRef(br);       /* gives the brush its handle, taking our reference */
}

export pageableuserexec brush_t CreatePatternedBrush(colour_t primary, colour_t secondary, int pattern) {
    return (brush_t) SystemCall4(SYS_CreatePatternedBrush, primary, secondary, pattern);
}

export pageableuserexec brush_t CreateSolidBrush(colour_t argb) {
    return CreatePatternedBrush(argb, argb, BRUSH_PATTERN_SOLID);
}
