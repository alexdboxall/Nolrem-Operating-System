
#include <obj.h>
#include <heap.h>
#include <log.h>
#include <mutex.h>
#include "winmgr_internal.h"

bool WmIsPointInRect(int x, int y, struct rect r) {
    return (x >= r.x && x < r.x + r.w && y >= r.y && y < r.y + r.h);
}

// ADDS A REFERENCE!
struct window* WmGetToplevelAtPoint(int x, int y, bool lock) {
    if (lock) WmLock();
    struct window* win = WmGetDesktop()->first_child;
    while (win) {
        if (WmIsPointInRect(x, y, WmGetGlobalPosition(win, false))) {
            RefObject(win);
            if (lock) WmUnlock();
            return win;
        }
        win = win->next_sibling;
    }
    if (lock) WmUnlock();
    return NULL;
}