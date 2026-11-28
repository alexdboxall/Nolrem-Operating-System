#include <common.h>
#include <usersys.h>
#include "wmsyscall_internal.h"

pageable size_t SysRaiseWindowToTop(size_t handle, size_t, size_t, size_t) {
    int err;
    struct window* win = WmSysOwnWindow(handle, &err);
    if (win == NULL) {
        return err;
    }
    WmRaiseToTop(win, true);
    KeDerefObject(win);
    return 0;
}

pageable size_t SysIsWindowAncestor(size_t ancestor_handle, size_t handle, size_t, size_t) {
    struct window* ancestor = WmSysWindow(ancestor_handle, NULL);
    if (ancestor == NULL) {
        return 0;
    }
    struct window* win = WmSysWindow(handle, NULL);
    if (win == NULL) {
        KeDerefObject(ancestor);
        return 0;
    }

    /* Walks the parents, so the tree has to stay still. */
    WmLock();
    bool result = WmIsAncestor(ancestor, win);
    WmUnlock();

    KeDerefObject(win);
    KeDerefObject(ancestor);
    return result;
}

pageable size_t SysGetMousePosition(size_t point_ptr, size_t, size_t, size_t) {
    struct point pt = WmGetMousePositionGlobal();
    struct transfer tr = KeCreateTransferWritingToUser((void*) point_ptr, sizeof(struct point), 0);
    int res = KePerformTransfer(&pt, &tr, sizeof(struct point));
    if (res != 0) {
        KeSetLastUserError(res);
    }
    return res;
}

pageable size_t SysInvalidateWindow(size_t handle, size_t, size_t, size_t) {
    int err;
    struct window* win = WmSysOwnWindow(handle, &err);
    if (win == NULL) {
        return err;
    }
    WmInvalidateWindow(win, true);
    KeDerefObject(win);
    return 0;
}

pageable size_t SysInvalidateWindowRect(size_t handle, size_t rect_ptr, size_t, size_t) {
    if (KeIsOutOfMemory()) {
        return WmSysFail(ENOMEM);
    }
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

    /*
     * The window manager works in screen coordinates, so we move the rectangle by where the window is. First it
     * is cut down to the window: anything outside is nothing to draw, and it also stops the sum going out of
     * the range that a region can hold, as the window's position can be anything that its parents add up to.
     */
    WmLock();
    int x1 = MAX((int) rect.x, 0);
    int y1 = MAX((int) rect.y, 0);
    int x2 = MIN((int) rect.x + rect.w, (int) win->local_win_bound.w);
    int y2 = MIN((int) rect.y + rect.h, (int) win->local_win_bound.h);
    if (x2 > x1 && y2 > y1) {
        struct region rgn = CdCreateRectRegion(win->global_offset_cached.x + x1, win->global_offset_cached.y + y1,
                                               x2 - x1, y2 - y1);
        WmInvalidateRegion(win, rgn, false);
        CdFreeRegion(rgn);
    }
    WmUnlock();

    KeDerefObject(win);
    return 0;
}

export pageableuserexec int RaiseWindowToTop(window_t win) {
    return (int) SystemCall2(SYS_RaiseWindowToTop, (size_t) win);
}

export pageableuserexec bool IsWindowAncestor(window_t ancestor, window_t win) {
    return (bool) SystemCall3(SYS_IsWindowAncestor, (size_t) ancestor, (size_t) win);
}

export pageableuserexec struct point GetMousePosition(void) {
    struct point pt = {0};
    size_t res = SystemCall2(SYS_GetMousePosition, (size_t) &pt);
    if (res != 0) {
        struct point zero = {0};
        SetLastError((int) res);
        return zero;
    }
    return pt;
}

export pageableuserexec int InvalidateWindow(window_t win) {
    return (int) SystemCall2(SYS_InvalidateWindow, (size_t) win);
}

export pageableuserexec int InvalidateWindowRect(window_t win, struct rect rect) {
    return (int) SystemCall3(SYS_InvalidateWindowRect, (size_t) win, (size_t) &rect);
}
