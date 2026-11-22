#include <common.h>
#include <obj.h>
#include <string.h>
#include <syscall.h>
#include "../api.h"
#include "../clipdraw_internal.h"

pageable size_t SysCreatePatternedPen(size_t colour, size_t thickness, size_t pattern, size_t) {
    if (KeIsOutOfMemory()) {
        KeSetLastUserError(ENOMEM);
        return 0;
    }
    /* Negative values become huge unsigned values, so these also reject them. */
    if (thickness < 1 || thickness > PEN_MAX_THICKNESS) {
        KeSetLastUserError(EINVAL);
        return 0;
    }
    if (pattern > _HIGHEST_USED_PEN_TYPE) {
        KeSetLastUserError(EINVAL);
        return 0;
    }

    struct pen* pen = CdCreatePatternedPen((colour_t) colour, (int) thickness, (int) pattern);
    if (pen == NULL) {
        KeSetLastUserError(ENOMEM);
        return 0;
    }
    return (size_t) pen;
}

export pageableuserexec pen_t CreatePatternedPen(colour_t colour, int thickness, int pattern) {
    return (pen_t) SystemCall4(SYS_CreatePatternedPen, colour, thickness, pattern);
}

export pageableuserexec pen_t CreateSolidPen(colour_t colour, int thickness) {
    return CreatePatternedPen(colour, thickness, PEN_PATTERN_SOLID);
}
