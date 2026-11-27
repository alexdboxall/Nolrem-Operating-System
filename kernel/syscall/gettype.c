#include <common.h>
#include <obj.h>
#include <string.h>
#include <syscall.h>
#include <uobj.h>

/* The type of the object that the handle is to, so a closed or bad handle is OBJ_INVALID. */
pageable size_t SysGetObjectType(size_t handle, size_t, size_t, size_t) {
    void* obj = KeValidateUserObjectAndAtomicallyRef((void*) handle, OBJ_ANYTYPE);
    if (obj == NULL) {
        return OBJ_INVALID;
    }
    int type = KeGetObjectType(obj);
    KeDerefObject(obj);
    return type;
}

export pageableuserexec int GetObjectType(any_t handle) {
    return SystemCall2(SYS_GetObjectType, (size_t) handle);
}

export pageableuserexec bool IsAnything(any_t handle) {
    return GetObjectType(handle) != OBJ_INVALID;
}