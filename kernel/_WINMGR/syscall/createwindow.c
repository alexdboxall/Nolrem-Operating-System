#include <common.h>
#include <usersys.h>
#include "wmsyscall_internal.h"

pageable size_t SysCreateWindow(size_t parent_handle, size_t class_handle, size_t rect_ptr, size_t) {
    if (KeIsOutOfMemory()) {
        KeSetLastUserError(ENOMEM);
        return 0;
    }
    struct app* app = WmSysGetApp();
    if (app == NULL) {
        return 0;
    }
    struct rect rect;
    if (WmSysReadRect(rect_ptr, &rect) != 0) {
        return 0;
    }

    /* No parent means the desktop, which anyone can make windows in. Anywhere else has to be our own. */
    struct window* parent;
    if (parent_handle == 0) {
        parent = WmSysRefWindow(WmGetDesktop());
    } else {
        parent = WmSysWindow(parent_handle, NULL);
        if (parent == NULL) {
            return 0;
        }
        if (parent != WmGetDesktop() && parent->app != app) {
            KeDerefObject(parent);
            KeSetLastUserError(EACCES);
            return 0;
        }
    }

    /*
     * Where a window is in the tree never changes, so counting the parents doesn't need the lock. This is
     * the check that WmCreateWindow also does, which is how we know that if it fails after this then it is for
     * lack of memory, and not for a reason that the user can do something about.
     */
    int depth = 0;
    for (struct window* par = parent; par != NULL; par = par->parent) {
        ++depth;
    }
    if (depth > MAX_NESTED_WIN_DEPTH) {
        KeDerefObject(parent);
        KeSetLastUserError(EINVAL);
        return 0;
    }

    struct window_class* wc;
    if (class_handle == 0) {
        wc = WmOpenWindowClass(NULL);
        if (wc == NULL) {
            KeSetLastUserError(ENOENT);
        }
    } else {
        wc = WmSysClass(class_handle, NULL);
    }
    if (wc == NULL || !WmSysClassIsUsable(wc)) {
        if (wc != NULL) {
            KeDerefObject(wc);
        }
        KeDerefObject(parent);
        return 0;
    }

    /*
     * The window manager makes windows from a class's name. Names are what it finds them by, and they never go
     * away, so this is the class that we have unless two have been given the same name (and then the newest wins).
     */
    struct window* win = WmCreateWindow(app, parent, wc->name, rect, true);
    KeDerefObject(wc);
    KeDerefObject(parent);
    if (win == NULL) {
        KeSetLastUserError(ENOMEM);
        return 0;
    }

    /*
     * What WmCreateWindow gives back is the tree's reference to the window, and not ours, so the handle needs one
     * of its own. If that can't be made then the window is still there, and we just don't have a way to it.
     */
    KeRefObject(win);
    return WmSysWindowHandle(win);
}

export pageableuserexec window_t CreateWindow(window_t parent, window_class_t cls, struct rect rect) {
    return (window_t) SystemCall4(SYS_CreateWindow, (size_t) parent, (size_t) cls, (size_t) &rect);
}
