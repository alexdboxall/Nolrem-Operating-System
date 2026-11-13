#include <obj.h>
#include <heap.h>
#include <log.h>
#include <string.h>
#include <spinlock.h>
#include <mutex.h>
#include <errno.h>
#include "winmgr_internal.h"

static struct spinlock winclass_lock;
static struct window_class* classes;

export struct window_class* WmCreateWindowClassEx(
    const char* name, 
    struct window_class* parent,
    int extra_ptrs,
    struct icon* icon,
    struct cursor* cursor,
    winproc_t proc, 
    int flags,
    int* extra_off_out
) {
    if (extra_ptrs < 0 || extra_ptrs > 8) {
        WmSetError(EINVAL);
        return NULL;
    }
    struct window_class* wc = AllocHeap(sizeof(struct window_class) + strlen(name) + 1);
    if (wc == NULL) {
        WmSetError(ENOMEM);
        return NULL;
    }
    if (parent != NULL) {
        RefObject(parent);
        if (parent->extra_ptrs + extra_ptrs > 16) {
            DerefObject(parent);
            FreeHeap(wc);
            WmSetError(EINVAL);
            return NULL;
        }
        extra_ptrs += parent->extra_ptrs;
        if (extra_off_out != NULL) {
            *extra_off_out = parent->extra_ptrs;
        }
    }
    InitUserObject(wc, UOBJ_WINDOW_CLASS);

    if (icon != NULL) {
        RefObject(icon);
    }
    if (cursor != NULL) {
        RefObject(cursor);
    }
    strcpy(wc->name, name);
    wc->proc = proc;
    wc->extra_ptrs = extra_ptrs;
    wc->parent = parent;
    wc->icon = icon;
    wc->cursor = cursor;

    AcquireSpinlock(&winclass_lock);
    wc->next = classes;
    wc->flags = flags;
    wc->extra_win_ptrs = 0;
    classes = wc;
    ReleaseSpinlock(&winclass_lock);

    WmSetError(0);
    return wc;
}

export struct window_class* WmCreateWindowClass(const char* name, winproc_t proc, int flags) {
    return WmCreateWindowClassEx(name, NULL, 0, NULL, NULL, proc, flags, NULL);
}

export struct window_class* WmOpenWindowClass(const char* _name) {
    AcquireSpinlock(&winclass_lock);
    struct window_class* wc = classes;
    const char* name = _name == NULL ? ".DEFAULT" : _name;
    while (wc) {
        if (!strcmp(wc->name, name)) {
            RefObject(wc);
            ReleaseSpinlock(&winclass_lock);
            return wc;
        }
        wc = wc->next;
    }
    ReleaseSpinlock(&winclass_lock);
    return NULL;
}

void WmInitWindowClassSubsystem() {
    InitSpinlock(&winclass_lock);
    classes = NULL;
    WmCreateWindowClass(".DEFAULT", WmDefaultWindowProcedure, 0);
}
