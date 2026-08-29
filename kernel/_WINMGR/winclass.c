#include <obj.h>
#include <heap.h>
#include <log.h>
#include <string.h>
#include <spinlock.h>
#include <mutex.h>
#include "winmgr_internal.h"

static struct spinlock winclass_lock;
static struct window_class* classes;

export struct window_class* WmCreateWindowClass(const char* name, winproc_t proc, int flags) {
    (void) flags;
    struct window_class* wc = AllocHeap(sizeof(struct window_class));
    InitUserObject(wc, UOBJ_WINDOW_CLASS);

    wc->name = KeStrdup(name);
    wc->proc = proc;

    AcquireSpinlock(&winclass_lock);
    wc->next = classes;
    wc->flags = flags;
    classes = wc;
    ReleaseSpinlock(&winclass_lock);

    return wc;
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