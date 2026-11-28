#include <common.h>
#include <usersys.h>
#include "wmsyscall_internal.h"

/* The longest name that OpenWindowClass() will look for, not counting the terminator. */
#define WM_MAX_CLASS_NAME_LENGTH    63

pageable size_t SysOpenWindowClass(size_t name_ptr, size_t, size_t, size_t) {
    if (KeIsOutOfMemory()) {
        KeSetLastUserError(ENOMEM);
        return 0;
    }

    struct window_class* wc;
    if (name_ptr == 0) {
        wc = WmOpenWindowClass(NULL);
    } else {
        /* One more than the longest allowed, so that a name that is too long can be told from one that just fits. */
        char name[WM_MAX_CLASS_NAME_LENGTH + 2];
        int res = KeReadStringFromUsermode(name, (const char*) name_ptr, sizeof(name));
        if (res != 0) {
            KeSetLastUserError(res);
            return 0;
        }
        if (strlen(name) > WM_MAX_CLASS_NAME_LENGTH) {
            KeSetLastUserError(ENAMETOOLONG);
            return 0;
        }
        wc = WmOpenWindowClass(name);
    }

    /* Comes back with a reference, which the handle takes. */
    if (wc == NULL) {
        KeSetLastUserError(ENOENT);
        return 0;
    }
    return WmSysClassHandle(wc);
}

pageable size_t SysGetWindowClass(size_t handle, size_t, size_t, size_t) {
    if (KeIsOutOfMemory()) {
        KeSetLastUserError(ENOMEM);
        return 0;
    }
    struct window* win = WmSysWindow(handle, NULL);
    if (win == NULL) {
        return 0;
    }

    /* The window keeps its class alive, and the class never changes, so it's safe to take a reference to it. */
    struct window_class* wc = win->winclass;
    KeRefObject(wc);
    KeDerefObject(win);
    return WmSysClassHandle(wc);
}

pageable size_t SysGetWindowClassName(size_t handle, size_t buffer, size_t size, size_t) {
    if (size == 0) {
        return WmSysFail(EINVAL);
    }
    int err;
    struct window_class* wc = WmSysClass(handle, &err);
    if (wc == NULL) {
        return err;
    }

    bool fits = strlen(wc->name) < size;
    int res = KeWriteStringToUsermode(wc->name, (char*) buffer, size);
    KeDerefObject(wc);
    if (res != 0) {
        return WmSysFail(res);
    }
    return fits ? 0 : WmSysFail(ERANGE);
}

export pageableuserexec window_class_t OpenWindowClass(const char* name) {
    return (window_class_t) SystemCall2(SYS_OpenWindowClass, (size_t) name);
}

export pageableuserexec window_class_t GetWindowClass(window_t win) {
    return (window_class_t) SystemCall2(SYS_GetWindowClass, (size_t) win);
}

export pageableuserexec int GetWindowClassName(window_class_t cls, char* buffer, size_t size) {
    return (int) SystemCall4(SYS_GetWindowClassName, (size_t) cls, (size_t) buffer, size);
}
