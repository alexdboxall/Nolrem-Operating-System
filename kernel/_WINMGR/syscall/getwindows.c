#include <common.h>
#include <usersys.h>
#include "wmsyscall_internal.h"

/* Gives a handle for a window that a lookup has found, and added a reference to for it. NULL means that there was none. */
static size_t WmSysFoundWindow(struct window* win) {
    if (win == NULL) {
        KeSetLastUserError(ENOENT);
        return 0;
    }
    return WmSysWindowHandle(win);
}

pageable size_t SysGetDesktopWindow(size_t, size_t, size_t, size_t) {
    if (KeIsOutOfMemory()) {
        KeSetLastUserError(ENOMEM);
        return 0;
    }
    return WmSysFoundWindow(WmSysRefWindow(WmGetDesktop()));
}

pageable size_t SysGetForegroundWindow(size_t, size_t, size_t, size_t) {
    if (KeIsOutOfMemory()) {
        KeSetLastUserError(ENOMEM);
        return 0;
    }
    /* The foreground window can be changed at any time, so we need the lock for it to still be there when we ref it. */
    WmLock();
    struct window* win = WmSysRefWindow(WmGetForegroundWindow());
    WmUnlock();
    return WmSysFoundWindow(win);
}

pageable size_t SysGetRelatedWindow(size_t handle, size_t relation, size_t, size_t) {
    if (relation != WINDOW_PARENT && relation != WINDOW_FIRST_CHILD && relation != WINDOW_NEXT_SIBLING) {
        KeSetLastUserError(EINVAL);
        return 0;
    }
    if (KeIsOutOfMemory()) {
        KeSetLastUserError(ENOMEM);
        return 0;
    }
    struct window* win = WmSysWindow(handle, NULL);
    if (win == NULL) {
        return 0;
    }

    WmLock();
    struct window* found;
    if (relation == WINDOW_PARENT) {
        found = win->parent;
    } else if (relation == WINDOW_FIRST_CHILD) {
        found = win->first_child;
    } else {
        found = win->next_sibling;
    }
    found = WmSysRefWindow(found);
    WmUnlock();

    KeDerefObject(win);
    return WmSysFoundWindow(found);
}

pageable size_t SysGetToplevelWindowAtPoint(size_t x, size_t y, size_t, size_t) {
    if (KeIsOutOfMemory()) {
        KeSetLastUserError(ENOMEM);
        return 0;
    }
    /* Comes back with a reference. */
    return WmSysFoundWindow(WmGetToplevelAtPoint((int) x, (int) y, true));
}

pageable size_t SysGetDeepestWindowAtPoint(size_t x, size_t y, size_t, size_t) {
    if (KeIsOutOfMemory()) {
        KeSetLastUserError(ENOMEM);
        return 0;
    }
    /* This one doesn't give a reference, so we take one while nothing can change. */
    WmLock();
    struct window* win = WmSysRefWindow(WmGetDeepestAtPoint((int) x, (int) y, false));
    WmUnlock();
    return WmSysFoundWindow(win);
}

export pageableuserexec window_t GetDesktopWindow(void) {
    return (window_t) SystemCall1(SYS_GetDesktopWindow);
}

export pageableuserexec window_t GetForegroundWindow(void) {
    return (window_t) SystemCall1(SYS_GetForegroundWindow);
}

export pageableuserexec window_t GetRelatedWindow(window_t win, int relation) {
    return (window_t) SystemCall3(SYS_GetRelatedWindow, (size_t) win, relation);
}

export pageableuserexec window_t GetToplevelWindowAtPoint(int x, int y) {
    return (window_t) SystemCall3(SYS_GetToplevelWindowAtPoint, x, y);
}

export pageableuserexec window_t GetDeepestWindowAtPoint(int x, int y) {
    return (window_t) SystemCall3(SYS_GetDeepestWindowAtPoint, x, y);
}
