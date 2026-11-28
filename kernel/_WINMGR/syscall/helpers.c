#include <common.h>
#include "wmsyscall_internal.h"

pageable struct app* WmSysGetApp(void) {
    struct app* app = WmGetAppObjForProcess();
    if (app == NULL) {
        KeSetLastUserError(EACCES);
    }
    return app;
}

/* Sets the last error, and gives the error to the caller if they want it. */
static void WmSysSetError(int* err, int value) {
    KeSetLastUserError(value);
    if (err != NULL) {
        *err = value;
    }
}

pageable struct window* WmSysWindow(size_t handle, int* err) {
    struct window* win = KeValidateUserObjectAndAtomicallyRef((void*) handle, OBJ_WINDOW);
    if (win == NULL) {
        WmSysSetError(err, EINVAL);
    }
    return win;
}

pageable struct window_class* WmSysClass(size_t handle, int* err) {
    struct window_class* wc = KeValidateUserObjectAndAtomicallyRef((void*) handle, OBJ_WINDOW_CLASS);
    if (wc == NULL) {
        WmSysSetError(err, EINVAL);
    }
    return wc;
}

pageable struct window* WmSysOwnWindow(size_t handle, int* err) {
    struct app* app = WmSysGetApp();
    if (app == NULL) {
        WmSysSetError(err, EACCES);
        return NULL;
    }
    struct window* win = WmSysWindow(handle, err);
    if (win == NULL) {
        return NULL;
    }

    /* A window's application is set when it is made and never changes, so there is no need for the lock. */
    if (win->app != app) {
        KeDerefObject(win);
        WmSysSetError(err, EACCES);
        return NULL;
    }
    return win;
}

pageable size_t WmSysWindowHandle(struct window* win) {
    return (size_t) KeCreateUserRef(win);
}

pageable size_t WmSysClassHandle(struct window_class* wc) {
    return (size_t) KeCreateUserRef(wc);
}

pageable struct window* WmSysRefWindow(struct window* win) {
    if (win != NULL) {
        KeRefObject(win);
    }
    return win;
}

pageable bool WmSysCheckRect(struct rect r) {
    if (r.x < -WINDOW_MAX_COORD || r.x > WINDOW_MAX_COORD ||
        r.y < -WINDOW_MAX_COORD || r.y > WINDOW_MAX_COORD ||
        r.w < 0 || r.w > WINDOW_MAX_COORD ||
        r.h < 0 || r.h > WINDOW_MAX_COORD) {
        KeSetLastUserError(EINVAL);
        return false;
    }
    return true;
}

pageable int WmSysReadRect(size_t user_ptr, struct rect* out) {
    struct rect r;
    struct transfer tr = KeCreateTransferReadingFromUser((const void*) user_ptr, sizeof(struct rect), 0);
    int res = KePerformTransfer(&r, &tr, sizeof(struct rect));
    if (res != 0) {
        KeSetLastUserError(res);
        return res;
    }
    if (!WmSysCheckRect(r)) {
        return EINVAL;
    }
    *out = r;
    return 0;
}

pageable int WmSysWriteRect(size_t user_ptr, struct rect rect) {
    struct transfer tr = KeCreateTransferWritingToUser((void*) user_ptr, sizeof(struct rect), 0);
    int res = KePerformTransfer(&rect, &tr, sizeof(struct rect));
    if (res != 0) {
        KeSetLastUserError(res);
    }
    return res;
}

pageable bool WmSysClassIsUsable(struct window_class* wc) {
    if (wc->name[0] == '.' && strcmp(wc->name, ".DEFAULT") != 0) {
        KeSetLastUserError(EACCES);
        return false;
    }
    return true;
}

pageable size_t WmSysFail(int err) {
    KeSetLastUserError(err);
    return (size_t) err;
}
