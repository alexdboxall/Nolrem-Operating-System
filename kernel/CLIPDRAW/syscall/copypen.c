#include <common.h>
#include <obj.h>
#include <string.h>
#include <syscall.h>
#include "../api.h"
#include "../clipdraw_internal.h"

pageable size_t SysCopyPen(size_t a, size_t, size_t, size_t) {
    if (KeIsOutOfMemory()) {
        KeSetLastUserError(ENOMEM);
        return 0;
    }
    struct pen* pen = KeValidateUserObjectAndAtomicallyRef((void*) a, OBJ_PEN);
    if (pen == NULL) {
        KeSetLastUserError(EINVAL);
        return 0;
    }

    struct pen* copy = CdCopyPen(pen);
    KeDerefObject(pen);

    if (copy == NULL) {
        KeSetLastUserError(ENOMEM);
        return 0;
    }
    return (size_t) KeCreateUserRef(copy);
}

export pageableuserexec pen_t CopyPen(pen_t pen) {
    return (pen_t) SystemCall2(SYS_CopyPen, (size_t) pen);
}
