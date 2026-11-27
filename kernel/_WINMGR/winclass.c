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
        KeSetLastUserError(EINVAL);
        return NULL;
    }
    struct window_class* wc = AllocHeap(sizeof(struct window_class) + strlen(name) + 1);
    if (wc == NULL) {
        KeSetLastUserError(ENOMEM);
        return NULL;
    }
    if (parent != NULL) {
        KeRefObject(parent);
        if (parent->extra_ptrs + extra_ptrs > 16) {
            KeDerefObject(parent);
            FreeHeap(wc);
            KeSetLastUserError(EINVAL);
            return NULL;
        }
        extra_ptrs += parent->extra_ptrs;
        if (extra_off_out != NULL) {
            *extra_off_out = parent->extra_ptrs;
        }
    }
    KeInitObject(wc, OBJ_WINDOW_CLASS);

    if (icon != NULL) {
        KeRefObject(icon);
    }
    if (cursor != NULL) {
        KeRefObject(cursor);
    }
    strcpy(wc->name, name);
    wc->proc = proc;
    wc->extra_ptrs = extra_ptrs;
    wc->parent = parent;
    wc->icon = icon;
    wc->cursor = cursor;
    wc->flags = flags;
    
    KeAcquireSpinlock(&winclass_lock);
    wc->next = classes;
    classes = wc;
    KeReleaseSpinlock(&winclass_lock);

    return wc;
}

export struct window_class* WmCreateWindowClass(const char* name, winproc_t proc, int flags) {
    return WmCreateWindowClassEx(name, NULL, 0, NULL, NULL, proc, flags, NULL);
}

export struct window_class* WmOpenWindowClass(const char* _name) {
    KeAcquireSpinlock(&winclass_lock);
    struct window_class* wc = classes;
    const char* name = _name == NULL ? ".DEFAULT" : _name;
    while (wc) {
        if (!strcmp(wc->name, name)) {
            KeRefObject(wc);
            KeReleaseSpinlock(&winclass_lock);
            return wc;
        }
        wc = wc->next;
    }
    KeReleaseSpinlock(&winclass_lock);
    return NULL;
}

void WmInitWindowClassSubsystem() {
    KeInitSpinlock(&winclass_lock);
    classes = NULL;
    WmCreateWindowClass(".DEFAULT", WmDefaultWindowProcedure, 0);
}
