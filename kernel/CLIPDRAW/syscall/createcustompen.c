#include <common.h>
#include <obj.h>
#include <string.h>
#include <transfer.h>
#include <syscall.h>
#include "../api.h"
#include "../clipdraw_internal.h"

#define CUSTOM_PEN_MAX_SIZE     8

pageable size_t SysCreateCustomPen(size_t colour, size_t width, size_t height, size_t rows_ptr) {
    if (KeIsOutOfMemory()) {
        KeSetLastUserError(ENOMEM);
        return 0;
    }
    if (width < 1 || width > CUSTOM_PEN_MAX_SIZE || height < 1 || height > CUSTOM_PEN_MAX_SIZE) {
        KeSetLastUserError(EINVAL);
        return 0;
    }

    /* One byte per row. */
    uint8_t rows[CUSTOM_PEN_MAX_SIZE];
    struct transfer tr = KeCreateTransferReadingFromUser((const void*) rows_ptr, height, 0);
    int res = KePerformTransfer(rows, &tr, height);
    if (res != 0) {
        KeSetLastUserError(res);
        return 0;
    }

    struct pen* pen = CdCreateCustomPen((colour_t) colour, (int) width, (int) height, rows);
    if (pen == NULL) {
        KeSetLastUserError(ENOMEM);
        return 0;
    }
    return (size_t) KeCreateUserRef(pen);      /* gives the pen its handle, taking our reference */
}

export pageableuserexec pen_t CreateCustomPen(colour_t colour, int width, int height, uint8_t* rows_bitmap) {
    return (pen_t) SystemCall(SYS_CreateCustomPen, colour, width, height, (size_t) rows_bitmap);
}
