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
    return SystemCall(SYS_GetObjectType, (size_t) handle, 0, 0, 0);
}

export pageableuserexec bool IsMailbox(any_t handle) {
    return GetObjectType(handle) == UOBJ_MAILBOX;
}

export pageableuserexec bool IsMutex(any_t handle) {
    return GetObjectType(handle) == UOBJ_MUTEX;
}

export pageableuserexec bool IsThread(any_t handle) {
    return GetObjectType(handle) == UOBJ_THREAD;
}

export pageableuserexec bool IsProcess(any_t handle) {
    return GetObjectType(handle) == UOBJ_PROCESS;
}

export pageableuserexec bool IsFile(any_t handle) {
    return GetObjectType(handle) == UOBJ_FILE;
}

export pageableuserexec bool IsDirectory(any_t handle) {
    return GetObjectType(handle) == UOBJ_DIRECTORY;
}

export pageableuserexec bool IsTimer(any_t handle) {
    return GetObjectType(handle) == UOBJ_TIMER;
}

export pageableuserexec bool IsMemoryRegion(any_t handle) {
    return GetObjectType(handle) == UOBJ_MEMORY_REGION;
}

export pageableuserexec bool IsSound(any_t handle) {
    return GetObjectType(handle) == UOBJ_SOUND;
}

export pageableuserexec bool IsDynamicLibrary(any_t handle) {
    return GetObjectType(handle) == UOBJ_DYNAMIC_LIBRARY;
}

export pageableuserexec bool IsWindow(any_t handle) {
    return GetObjectType(handle) == UOBJ_WINDOW;
}

export pageableuserexec bool IsWindowClass(any_t handle) {
    return GetObjectType(handle) == UOBJ_WINDOW_CLASS;
}

export pageableuserexec bool IsBrush(any_t handle) {
    return GetObjectType(handle) == UOBJ_BRUSH;
}

export pageableuserexec bool IsPen(any_t handle) {
    return GetObjectType(handle) == UOBJ_PEN;
}

export pageableuserexec bool IsBitmap(any_t handle) {
    return GetObjectType(handle) == UOBJ_BITMAP;
}

export pageableuserexec bool IsTypeface(any_t handle) {
    return GetObjectType(handle) == UOBJ_TYPEFACE;
}

export pageableuserexec bool IsFont(any_t handle) {
    return GetObjectType(handle) == UOBJ_FONT;
}

export pageableuserexec bool IsRegion(any_t handle) {
    return GetObjectType(handle) == UOBJ_REGION;
}

export pageableuserexec bool IsDc(any_t handle) {
    return GetObjectType(handle) == UOBJ_DC;
}

export pageableuserexec bool IsAnything(any_t handle) {
    return GetObjectType(handle) != UOBJ_INVALID;
}