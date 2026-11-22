#include <common.h>
#include <obj.h>
#include <string.h>
#include <transfer.h>
#include <syscall.h>
#include "../api.h"
#include "../clipdraw_internal.h"

/* An 8x8 dot pattern, one byte per row. */
#define CUSTOM_BRUSH_PATTERN_BYTES  8

pageable size_t SysCreateCustomBrush(size_t primary, size_t secondary, size_t pattern_ptr, size_t) {
    if (IsOutOfMemory()) {
        KeSetLastUserError(ENOMEM);
        return 0;
    }

    uint8_t pattern[CUSTOM_BRUSH_PATTERN_BYTES];
    struct transfer tr = CreateTransferReadingFromUser((const void*) pattern_ptr, sizeof(pattern), 0);
    int res = PerformTransfer(pattern, &tr, sizeof(pattern));
    if (res != 0) {
        KeSetLastUserError(res);
        return 0;
    }

    struct brush* br = CdCreateCustomBrush((colour_t) primary, (colour_t) secondary, pattern);
    if (br == NULL) {
        KeSetLastUserError(ENOMEM);
        return 0;
    }
    return (size_t) br;
}

export pageableuserexec brush_t CreateCustomBrush(colour_t primary, colour_t secondary, uint8_t* pattern) {
    return (brush_t) SystemCall4(SYS_CreateCustomBrush, primary, secondary, (size_t) pattern);
}
