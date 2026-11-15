#include <common.h>
#include <obj.h>
#include <string.h>
#include <syscall.h>
#include "../api.h"
#include "../clipdraw_internal.h"

pageable size_t SysGetObjectType(size_t handle, size_t, size_t, size_t) {
    struct user_obj_header* uo = (void*) handle;
    if (!ValidateUserObjectAndAtomicallyRef(uo, UOBJ_ANYTYPE)) {
        return UOBJ_INVALID;
    }
    int type = uo->user_type;
    DerefObject(uo);
    return type;
}

export pageableuserexec int GetObjectType(any_t handle) {
    return SystemCall2(SYS_GetObjectType, (size_t) handle);
}

export pageableuserexec bool IsAnything(any_t handle) {
    return GetObjectType(handle) != UOBJ_INVALID;
}