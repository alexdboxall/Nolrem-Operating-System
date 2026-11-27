#include <common.h>
#include <obj.h>
#include <string.h>
#include <syscall.h>
#include "../api.h"
#include "../clipdraw_internal.h"

pageable size_t SysCopyBrush(size_t a, size_t, size_t, size_t) {
    if (KeIsOutOfMemory()) {
        KeSetLastUserError(ENOMEM);
        return 0;
    }
    struct brush* br = KeValidateUserObjectAndAtomicallyRef((void*) a, OBJ_BRUSH);
    if (br == NULL) {
        KeSetLastUserError(EINVAL);
        return 0;
    }

    struct brush* copy = CdCopyBrush(br);
    KeDerefObject(br);

    if (copy == NULL) {
        KeSetLastUserError(ENOMEM);
        return 0;
    }
    return (size_t) KeCreateUserRef(copy);
}

export pageableuserexec brush_t CopyBrush(brush_t br) {
    return (brush_t) SystemCall2(SYS_CopyBrush, (size_t) br);
}
