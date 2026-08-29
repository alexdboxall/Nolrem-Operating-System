#include <obj.h>
#include <heap.h>
#include <log.h>
#include <mutex.h>
#include "winmgr_internal.h"

export struct window_class* WmCreateWindowClass(const char* name, winproc_t proc) {
    struct window_class* wc = AllocHeap(sizeof(struct window_class));
    InitUserObject(wc, UOBJ_WINDOW_CLASS);

    wc->name = KeStrdup(name);
    wc->proc = proc;

    return wc;
}
