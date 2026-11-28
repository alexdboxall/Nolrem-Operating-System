#include <common.h>
#include <usersys.h>
#include "wmsyscall_internal.h"

#define WM_KNOWN_STYLES     (WS_MAXIMISED | WS_HIDDEN)

pageable size_t SysSetWindowRect(size_t handle, size_t rect_ptr, size_t, size_t) {
    struct rect rect;
    int read_res = WmSysReadRect(rect_ptr, &rect);
    if (read_res != 0) {
        return read_res;
    }
    int err;
    struct window* win = WmSysOwnWindow(handle, &err);
    if (win == NULL) {
        return err;
    }
    WmChangePosition(win, rect, true);
    KeDerefObject(win);
    return 0;
}

pageable size_t SysGetWindowRect(size_t rect_ptr, size_t handle, size_t, size_t) {
    struct window* win = WmSysWindow(handle, NULL);
    if (win == NULL) {
        return EINVAL;
    }
    WmLock();
    struct rect rect = win->local_win_bound;
    WmUnlock();
    KeDerefObject(win);
    return WmSysWriteRect(rect_ptr, rect);
}

pageable size_t SysGetWindowClientRect(size_t rect_ptr, size_t handle, size_t, size_t) {
    struct window* win = WmSysWindow(handle, NULL);
    if (win == NULL) {
        return EINVAL;
    }

    /* The client rectangle is kept in the parent's coordinates, like the window's. */
    WmLock();
    struct rect outer = win->local_win_bound;
    struct rect client = win->local_client_bound;
    WmUnlock();
    KeDerefObject(win);

    struct rect rect = {
        .x = (int16_t) (client.x - outer.x),
        .y = (int16_t) (client.y - outer.y),
        .w = client.w,
        .h = client.h,
    };
    return WmSysWriteRect(rect_ptr, rect);
}

pageable size_t SysGetWindowGlobalRect(size_t rect_ptr, size_t handle, size_t, size_t) {
    struct window* win = WmSysWindow(handle, NULL);
    if (win == NULL) {
        return EINVAL;
    }
    struct rect rect = WmGetGlobalPosition(win, true);
    KeDerefObject(win);
    return WmSysWriteRect(rect_ptr, rect);
}

pageable size_t SysAdjustWindowRect(size_t rect_ptr, size_t handle, size_t mode, size_t) {
    struct rect rect;
    int read_res = WmSysReadRect(rect_ptr, &rect);
    if (read_res != 0) {
        return read_res;
    }
    struct window* win = WmSysWindow(handle, NULL);
    if (win == NULL) {
        return EINVAL;
    }
    int res = WmAdjustWindowRect(&rect, win, NULL, 0, (int) mode);
    KeDerefObject(win);
    if (res != 0) {
        return WmSysFail(res);
    }
    return WmSysWriteRect(rect_ptr, rect);
}

pageable size_t SysAdjustWindowRectForClass(size_t rect_ptr, size_t class_handle, size_t style, size_t mode) {
    if ((int) style & ~WM_KNOWN_STYLES) {
        return WmSysFail(EINVAL);
    }
    struct rect rect;
    int read_res = WmSysReadRect(rect_ptr, &rect);
    if (read_res != 0) {
        return read_res;
    }
    struct window_class* wc = WmSysClass(class_handle, NULL);
    if (wc == NULL) {
        return EINVAL;
    }
    int res = WmAdjustWindowRect(&rect, NULL, wc, (int) style, (int) mode);
    KeDerefObject(wc);
    if (res != 0) {
        return WmSysFail(res);
    }
    return WmSysWriteRect(rect_ptr, rect);
}

export pageableuserexec int SetWindowRect(window_t win, struct rect rect) {
    return (int) SystemCall3(SYS_SetWindowRect, (size_t) win, (size_t) &rect);
}

export pageableuserexec struct rect GetWindowRect(window_t win) {
    struct rect r = {0};
    size_t res = SystemCall3(SYS_GetWindowRect, (size_t) &r, (size_t) win);
    if (res != 0) {
        struct rect zero = {0};
        SetLastError((int) res);
        return zero;
    }
    return r;
}

export pageableuserexec struct rect GetWindowClientRect(window_t win) {
    struct rect r = {0};
    size_t res = SystemCall3(SYS_GetWindowClientRect, (size_t) &r, (size_t) win);
    if (res != 0) {
        struct rect zero = {0};
        SetLastError((int) res);
        return zero;
    }
    return r;
}

export pageableuserexec struct rect GetWindowGlobalRect(window_t win) {
    struct rect r = {0};
    size_t res = SystemCall3(SYS_GetWindowGlobalRect, (size_t) &r, (size_t) win);
    if (res != 0) {
        struct rect zero = {0};
        SetLastError((int) res);
        return zero;
    }
    return r;
}

export pageableuserexec int AdjustWindowRect(struct rect* rect, window_t win, int mode) {
    return (int) SystemCall4(SYS_AdjustWindowRect, (size_t) rect, (size_t) win, mode);
}

export pageableuserexec int AdjustWindowRectForClass(struct rect* rect, window_class_t cls, int style, int mode) {
    return (int) SystemCall(SYS_AdjustWindowRectForClass, (size_t) rect, (size_t) cls, style, mode);
}
