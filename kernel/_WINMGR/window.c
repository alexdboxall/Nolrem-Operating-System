#include <obj.h>
#include <heap.h>
#include <log.h>
#include <mutex.h>
#include <kgfx.h>
#include <input.h>
#include <errno.h>
#include <harderror.h>
#include "winmgr_internal.h"

#define MAX_NESTED_WIN_DEPTH 6

static struct spinlock winmgr_lock;
static struct window* foreground_window = NULL;

bool anything_happened = false;

export struct window* WmGetForegroundWindow() {
    return foreground_window;
}

/*
 * Changes which window is in the foreground. The window manager's lock must be held.
 *
 * Nobody can be told about it from in here, as telling a window is a message to its procedure, which needs the lock
 * (see WmDispatchMessage). So `*change` is filled in with what has to be announced, and holds references to what it
 * names. Once the lock is let go of, give it to WmAnnounceActivation() (or WmDiscardActivation() if no-one is to be
 * told). `how` is how the new foreground window got there: WA_ACTIVATE, or WA_CLICKACTIVATE if the user clicked on it.
 *
 * If `win` already is the foreground window nothing changes, and `*change` is empty (and safe to give to either).
 */
void WmChangeForegroundWindow(struct window* win, int how, struct wm_activation* change) {
    change->old_fg = NULL;
    change->new_fg = NULL;
    change->how = how;

    struct window* old_fg = foreground_window;
    if (win == old_fg) {
        return;
    }

    if (old_fg != NULL) {
        WmInvalidateWindow(old_fg, false);
    }
    foreground_window = win;
    if (win != NULL) {
        KeRefObject(win);       /* the foreground window's own */
        KeRefObject(win);       /* the change's */
        change->new_fg = win;
        WmInvalidateWindow(win, false);
    }
    if (old_fg != NULL) {
        if (old_fg == WmGetDesktop()) {
            /* Nothing is told that the desktop stopped being the foreground window. */
            KeDerefObject(old_fg);
        } else {
            change->old_fg = old_fg;    /* the foreground window's reference goes to the change */
        }
    }
}

void WmDiscardActivation(struct wm_activation* change) {
    if (change->old_fg != NULL) {
        KeDerefObject(change->old_fg);
        change->old_fg = NULL;
    }
    if (change->new_fg != NULL) {
        KeDerefObject(change->new_fg);
        change->new_fg = NULL;
    }
}

/*
 * Tells the windows that were involved in a change of foreground window (see WmChangeForegroundWindow), and
 * gives back the references the change holds. The window manager's lock must NOT be held.
 *
 * WM_ACTIVATE goes to the window that lost it (WA_DEACTIVATE) and to the one that gained it (`how`). When the
 * application changes too, WM_ACTIVATE_APP goes the same way. A window that stays within the same application is
 * a switch within it, so the one losing it is told straight away, to keep the order of events. When it is another
 * application's window the message is posted to that application, as that is where it is dealt with. Windows
 * that don't belong to an application can't be posted to, and are told straight away.
 */
void WmAnnounceActivation(struct wm_activation* change) {
    struct window* old_fg = change->old_fg;
    struct window* new_fg = change->new_fg;

    bool app_changed = old_fg == NULL || new_fg == NULL || old_fg->app != new_fg->app;

    if (old_fg != NULL) {
        bool local = old_fg->app == NULL || (new_fg != NULL && old_fg->app == new_fg->app);
        WmPostOrDispatchMessage((struct msg) {
            .win = old_fg,
            .type = WM_ACTIVATE,
            .i_arg = WA_DEACTIVATE,
        }, local);

        if (app_changed && old_fg->app != NULL) {
            WmPostMessage((struct msg) {
                .win = old_fg,
                .type = WM_ACTIVATE_APP,
                .i_arg = WA_DEACTIVATE,
            });
        }
    }
    if (new_fg != NULL) {
        if (app_changed && new_fg->app != NULL) {
            WmDispatchMessage((struct msg) {
                .win = new_fg,
                .type = WM_ACTIVATE_APP,
                .i_arg = WA_ACTIVATE,
            });
        }
        WmDispatchMessage((struct msg) {
            .win = new_fg,
            .type = WM_ACTIVATE,
            .i_arg = change->how,
        });
    }
    WmDiscardActivation(change);
}

export void WmSetForegroundWindow(struct window* win) {
    struct wm_activation change;
    WmLock();
    WmChangeForegroundWindow(win, WA_ACTIVATE, &change);
    WmUnlock();
    WmAnnounceActivation(&change);
}

static void WmCleanupWindow(void* _win) {
    struct window* win = _win;
    KeDerefObject(win->winclass);
    FreeHeap(win);
}

void WmInitWindowSubsystem(void) {
    KeRegisterObjectType(OBJ_WINDOW, WmCleanupWindow);
}

void WmLock(void) {
    KeAcquireSpinlock(&winmgr_lock);
}

void WmUnlock(void) {
    KeReleaseSpinlock(&winmgr_lock);
}

static void WmAssertLocked(void) {
}

export bool WmIsAncestor(struct window* potential_ancestor, struct window* reference) {
    if (reference == potential_ancestor || reference == NULL || potential_ancestor == NULL) {
        return false;
    }
    
    WmAssertLocked();
    while (reference->parent) {
        reference = reference->parent;
        if (reference == potential_ancestor) {
            return true;
        }
    }
    return false;
}

struct rect WmGetGlobalPosition(struct window* win, bool lock) {
    if (lock) WmLock();
    struct rect r = {
        .x = win->global_offset_cached.x,
        .y = win->global_offset_cached.y,
        .w = win->local_win_bound.w,
        .h = win->local_win_bound.h
    };
    if (lock) WmUnlock();
    return r;
}

export void WmInvalidateRegion(struct window* win, struct region rgn, bool lock) {
    if (lock) WmLock();
    anything_happened = true;
    CdUnionRegionInPlace(&win->dirty_rgn, rgn);
    struct window* kiddo = win->first_child;
    while (kiddo) {
        WmInvalidateRegion(kiddo, rgn, false);
        kiddo = kiddo->next_sibling;
    }
    if (lock) WmUnlock();
}

export void WmInvalidateWindow(struct window* win, bool lock) {
    if (lock) WmLock();
    WmInvalidateRegion(win, win->win_rgn, false);
    if (lock) WmUnlock();
}

static struct point WmAccumulateScreenOrigin(struct window* win, bool lock) {
    if (lock) WmLock();
    int x = win->local_win_bound.x;
    int y = win->local_win_bound.y;
    for (struct window* p = win->parent; p != NULL; p = p->parent) {
        x += p->local_win_bound.x;
        y += p->local_win_bound.y;
    }
    if (lock) WmUnlock();
    return (struct point) {.x = x, .y = y};
}

static bool WmInvalidateExposedRegionOnWindow(struct window* win, struct region* exposed_rgn, bool actually_cover) {
    struct region overlap = CdIntersectRegion(win->win_rgn, *exposed_rgn);
    if (!CdIsRegionEmpty(overlap)) {
        if (actually_cover) {
            CdSubtractRegionInPlace(&win->vis_rgn, overlap);
        } else {
            CdUnionRegionInPlace(&win->vis_rgn, overlap);
        }
        CdUnionRegionInPlace(&win->dirty_rgn, overlap);
        CdSubtractRegionInPlace(exposed_rgn, overlap);
        CdFreeRegion(overlap);
        return CdIsRegionEmpty(*exposed_rgn);
    } else {
        CdFreeRegion(overlap);
        return false;
    }
}

/* Applies to `win` and its whole subtree. Children first, since they're in front. */
static bool WmApplyExposureToSubtree(struct window* win, struct region* exposed_rgn, bool actually_cover) {
    struct region claimed = CdIntersectRegion(win->win_rgn, *exposed_rgn);
    if (CdIsRegionEmpty(claimed)) {
        CdFreeRegion(claimed);
        return false;
    }

    /* The whole of `claimed` is opaquely accounted for by this subtree, so nothing
       further back may have it. Do this now, before the children eat into it. */
    CdSubtractRegionInPlace(exposed_rgn, claimed);

    struct region remaining = CdCopyRegion(claimed);
    for (struct window* kiddo = win->first_child; kiddo != NULL; kiddo = kiddo->next_sibling) {
        if (WmApplyExposureToSubtree(kiddo, &remaining, actually_cover)) {
            break;
        }
    }

    /* Whatever the children didn't want belongs to us. */
    if (!CdIsRegionEmpty(remaining)) {
        if (actually_cover) {
            CdSubtractRegionInPlace(&win->vis_rgn, remaining);
        } else {
            CdUnionRegionInPlace(&win->vis_rgn, remaining);
        }
        CdUnionRegionInPlace(&win->dirty_rgn, remaining);
    }

    CdFreeRegion(remaining);
    CdFreeRegion(claimed);
    return CdIsRegionEmpty(*exposed_rgn);
}

static void WmInvalidateExposedRegion(struct window* win, struct region* exposed_rgn, bool actually_cover) {
    if (!actually_cover) {
        /* Anything still covered by a sibling in front of win stays hidden no matter
           what win just did. Don't let that area leak through to windows behind win
           or the parent — they'd paint over whatever's legitimately on top there. */
        struct window* big_bro = win->parent ? win->parent->first_child : NULL;
        while (big_bro != NULL && big_bro != win) {
            CdSubtractRegionInPlace(exposed_rgn, big_bro->win_rgn);
            if (CdIsRegionEmpty(*exposed_rgn)) {
                return;
            }
            big_bro = big_bro->next_sibling;
        }
    }

    struct window* bro = win->next_sibling;
    while (bro) {
        bool now_empty = WmApplyExposureToSubtree(bro, exposed_rgn, actually_cover);
        if (now_empty) {
            return;
        }
        bro = bro->next_sibling;
    }
    if (win->parent) {
        WmInvalidateExposedRegionOnWindow(win->parent, exposed_rgn, actually_cover);
    }
}

static bool WmDoesWindowShowBorderEx(int winstyle, int classflags) {
    return !(winstyle & WS_MAXIMISED) && !(classflags & CS_ALLCLIENT);
}

bool WmDoesWindowShowBorder(struct window* win) {
    if (win == NULL || win->winclass == NULL) {
        return false;
    }
    return WmDoesWindowShowBorderEx(win->style, win->winclass->flags);
}

/*
 * The region (in screen coordinates) that children of `parent` are clipped to.
 * This is the parent's client area. Windows whose class is CS_ALLCLIENT have no
 * non-client area (see WmBeginPaint), so for those the client area is the whole
 * window. The returned region is borrowed from `parent` - do not free it.
 *
 * Both regions are themselves already clipped against the parent's own parent
 * (see WmSetInternalWindowBounds), so clipping against this clips against every
 * ancestor, not just the immediate one.
 */
static struct region WmGetChildClipRegion(struct window* parent) {
    if (parent->winclass->flags & CS_ALLCLIENT) {
        return parent->win_rgn;
    }
    return parent->client_rgn;
}


#define ADJUST_CLIENT_TO_OUTER  (-1)
#define ADJUST_OUTER_TO_CLIENT  ( 1)

/* Supply EITHER `win` or the pair `wc` and `style`. If win != NULL, wc and style ignored. */
export int WmAdjustWindowRect(struct rect* rect, struct window* win, struct window_class* wc, int style, int mode) {
    if (rect == NULL) {
        return EINVAL;
    }
    if (win == NULL && wc == NULL) {
        return EINVAL;
    }
    if (mode != ADJUST_CLIENT_TO_OUTER && mode != ADJUST_OUTER_TO_CLIENT) {
        return EINVAL;
    }
    bool has_border;
    bool all_client;
    if (win == NULL) {
        has_border = WmDoesWindowShowBorderEx(style, wc->flags);
        all_client = !!(wc->flags & CS_ALLCLIENT);
    } else {
        has_border = WmDoesWindowShowBorder(win);
        all_client = !!(win->winclass->flags & CS_ALLCLIENT);
    }
    if (all_client) {
        return 0;
    }
    int border_width = has_border ? BORDER_WIDTH : 0;
    int shadow = has_border ? SHADOW_CUT_IN : 0;
    rect->x += mode * (border_width);
    rect->y += mode * (border_width + TITLEBAR_HEIGHT);
    rect->w = MAX(0, rect->w - mode * (border_width * 2 + shadow));
    rect->h = MAX(0, rect->h - mode * (border_width * 2 + TITLEBAR_HEIGHT + shadow));
    return 0;
}

/*
 * Requires the parent's win_rgn/client_rgn to be up to date. That holds for
 * WmCreateWindow (the parent already exists) and for WmChangePosition (a window
 * is updated before the recursion into its children).
 */
static void WmSetInternalWindowBounds(struct window* win, struct rect local_r) {
    win->local_win_bound = local_r;
    win->local_client_bound = local_r;
    WmAdjustWindowRect(&win->local_client_bound, win, NULL, 0, ADJUST_OUTER_TO_CLIENT);
    win->global_offset_cached = WmAccumulateScreenOrigin(win, false);

    struct region raw_win_rgn = CdCreateRectRegion(
        win->global_offset_cached.x,
        win->global_offset_cached.y,
        win->local_win_bound.w,
        win->local_win_bound.h
    );
    struct region raw_client_rgn = CdCreateRectRegion(
        win->global_offset_cached.x + win->local_client_bound.x - win->local_win_bound.x,
        win->global_offset_cached.y + win->local_client_bound.y - win->local_win_bound.y,
        win->local_client_bound.w,
        win->local_client_bound.h
    );

    /* A child must never extend past its parent's client area. */
    if (win->parent != NULL) {
        win->win_rgn = CdIntersectRegion(raw_win_rgn, WmGetChildClipRegion(win->parent));
        CdFreeRegion(raw_win_rgn);
    } else {
        win->win_rgn = raw_win_rgn;
    }

    /* The client area is always a subset of the (clipped) window. */
    win->client_rgn = CdIntersectRegion(raw_client_rgn, win->win_rgn);
    CdFreeRegion(raw_client_rgn);
}

static void WmClipOurVisibility(struct window* win) {
    /* 
     * Clip against earlier siblings. 
     * Do this if `win` is a toplevel, or WS_CLIPSIBLINGS set.
     */
    struct window* big_bro = win->parent == NULL ? NULL : win->parent->first_child;
    while (big_bro != NULL && big_bro != win) {
        CdSubtractRegionInPlace(&win->vis_rgn, big_bro->win_rgn);
        if (CdIsRegionEmpty(win->vis_rgn)) {
            break;
        }
        big_bro = big_bro->next_sibling;
    }

    /* 
     * Clip against children.
     * Do this if WS_CLIPCHILDREN is set.
     */
    struct window* kiddo = win->first_child;
    while (kiddo != NULL) {
        CdSubtractRegionInPlace(&win->vis_rgn, kiddo->win_rgn);
        if (CdIsRegionEmpty(win->vis_rgn)) {
            break;
        }
        kiddo = kiddo->next_sibling;
    }
}

export void WmChangePosition(struct window* win, struct rect local_r, bool lock) {
    if (lock) WmLock();

    anything_happened = true;

    /* The easy bit. */
    struct region old_win_bounds = win->win_rgn;
    CdFreeRegion(win->client_rgn);
    WmSetInternalWindowBounds(win, local_r);
    
    /* Add to visible regions of what we exposed. */
    struct region exposed_rgn = CdSubtractRegion(old_win_bounds, win->win_rgn);
    if (!CdIsRegionEmpty(exposed_rgn)) {
        WmInvalidateExposedRegion(win, &exposed_rgn, false);
    }
    CdFreeRegion(exposed_rgn);

    /* Remove from the visible regions of what we covered. */
    struct region covered_rgn = CdSubtractRegion(win->win_rgn, old_win_bounds);
    if (!CdIsRegionEmpty(covered_rgn)) {
        WmInvalidateExposedRegion(win, &covered_rgn, true);
    }
    CdFreeRegion(covered_rgn);
    CdFreeRegion(old_win_bounds);       /* this clears old win->win_rgn */   

    /* Make the window dirty. */
    CdFreeRegion(win->dirty_rgn);
    win->dirty_rgn = CdCopyRegion(win->win_rgn);

    /* 
     * Tell all the children that their parents moved and to update their
     * regions also.
     */
    struct window* kiddo = win->first_child;
    while (kiddo) {
        WmChangePosition(kiddo, kiddo->local_win_bound, false);
        kiddo = kiddo->next_sibling;
    }

    /* Figure out our own visible region. */
    CdFreeRegion(win->vis_rgn);
    win->vis_rgn = CdCopyRegion(win->win_rgn);
    WmClipOurVisibility(win);

    if (lock) WmUnlock();
}

export int WmCallWinProc(struct window* win, struct msg msg) {
    return win->winclass->proc(win, msg);
}

export struct window* WmCreateWindow(struct app* app, struct window* parent, const char* classname, struct rect local_r, bool lock) {
    struct window_class* wc = WmOpenWindowClass(classname);
    if (wc == NULL) {
        return NULL;
    }
    
    struct window* win = AllocHeap(sizeof(struct window) + wc->extra_ptrs * sizeof(void*));
    if (win == NULL) {
        KeDerefObject(wc);      /* WmOpenWindowClass took a reference for the window we failed to make */
        return NULL;
    }
    KeInitObject(win, OBJ_WINDOW);

    if (lock) WmLock();

    struct window* par = parent;
    int depth = 0;
    while (par != NULL) {
        par = par->parent;
        ++depth;
    }
    if (depth > MAX_NESTED_WIN_DEPTH) {
        if (lock) WmUnlock();
        /* Don't deref as the win->winclass not set yet and the cleanup code
         * would try to free that. */
        FreeHeap(win);
        KeDerefObject(wc);      /* ... which also means nothing else will give back our class reference */
        return NULL;
    }

    anything_happened = true;

    /* Link into the app's window list. */
    if (app != NULL) {
        win->app_next_win = app->win_list_head;
        app->win_list_head = win;
        KeRefObject(app);
    }
   
    win->app = app;
    win->restore_pos = local_r;
    win->style = 0;
    win->winclass = wc;
    win->first_child = NULL;
    win->parent = parent;
    win->next_sibling = parent ? parent->first_child : NULL;
    if (parent) {
        parent->first_child = win;
    }

    WmSetInternalWindowBounds(win, local_r);

    struct region covered_rgn = CdCopyRegion(win->win_rgn);
    if (!CdIsRegionEmpty(covered_rgn)) {
        WmInvalidateExposedRegion(win, &covered_rgn, true);
    }

    CdFreeRegion(covered_rgn);
    win->dirty_rgn = CdCopyRegion(win->win_rgn);
    win->vis_rgn = CdCopyRegion(win->win_rgn);
    WmClipOurVisibility(win);

    /* A new toplevel window comes up in front, and is the one the user is working with. */
    struct wm_activation activation;
    activation.old_fg = NULL;
    activation.new_fg = NULL;
    if (parent != NULL && parent == WmGetDesktop()) {
        WmChangeForegroundWindow(win, WA_ACTIVATE, &activation);
    }
    
    if (lock) WmUnlock();

    WmDispatchMessage((struct msg) {
        .win = win,
        .type = WM_CREATE,
    });

    if (lock) {
        WmAnnounceActivation(&activation);
    } else {
        /* The caller still has the lock, so can't be told (nor can it be done from in here). */
        WmDiscardActivation(&activation);
    }

    return win;
}

/* Recomputes vis_rgn for win and its whole subtree from scratch (copy of win_rgn,
   clipped against own earlier siblings and own children), and marks whatever
   newly became visible as dirty. Only valid to call on a window (and its
   descendants) that currently has nothing external occluding it - e.g. right
   after it's been made topmost among its own siblings. */
static void WmRecomputeVisibilityRecursive(struct window* win) {
    struct region old_vis = win->vis_rgn;
    win->vis_rgn = CdCopyRegion(win->win_rgn);
    WmClipOurVisibility(win);

    struct region newly_visible = CdSubtractRegion(win->vis_rgn, old_vis);
    if (!CdIsRegionEmpty(newly_visible)) {
        CdUnionRegionInPlace(&win->dirty_rgn, newly_visible);
    }
    CdFreeRegion(newly_visible);
    CdFreeRegion(old_vis);

    for (struct window* kiddo = win->first_child; kiddo != NULL; kiddo = kiddo->next_sibling) {
        WmRecomputeVisibilityRecursive(kiddo);
    }
}

export void WmRaiseToTop(struct window* win, bool lock) {
    if (lock) WmLock();

    anything_happened = true;

    if (win->parent != NULL && win->parent->first_child != win) {
        /* Unlink win, then relink it at the front of its parent's child list. */
        struct window* prev = win->parent->first_child;
        while (prev->next_sibling != win) {
            prev = prev->next_sibling;
        }
        prev->next_sibling = win->next_sibling;
        win->next_sibling = win->parent->first_child;
        win->parent->first_child = win;

        /* win, and everything in its subtree, is now topmost among its top-level
           siblings - nothing outside the subtree can occlude any of it any more.
           Recompute vis_rgn for win AND its descendants, so children like B get
           back whatever area was stolen while something else (e.g. C) covered
           them. */
        WmRecomputeVisibilityRecursive(win);

        /* Whatever win now sits in front of needs win's shape punched out of its
           vis_rgn. Reuses the same pass WmChangePosition/WmCreateWindow use for
           "covered". For siblings that were already behind win, this is a no-op -
           their vis_rgn already excludes win's shape from when win was first
           created/positioned there. */
        struct region covered_rgn = CdCopyRegion(win->win_rgn);
        WmInvalidateExposedRegion(win, &covered_rgn, true);
        CdFreeRegion(covered_rgn);
    }

    if (lock) WmUnlock();
}

export bool WmCheckIfPaintRequired(struct window* win) {
    if (CdIsRegionEmpty(win->dirty_rgn)) {
        return false;
    }
    if (CdIsRegionEmpty(win->vis_rgn)) {
        return false;
    }
    struct region invl_rgn = CdIntersectRegion(win->vis_rgn, win->dirty_rgn);
    bool empty = CdIsRegionEmpty(invl_rgn);
    CdFreeRegion(invl_rgn);
    return !empty;
}

extern void WmInitDcCache(void);
extern void WmInitWindowClassSubsystem();
extern void WmInitDesktopWindowSubsystem();
extern void WmInitAppSubsystem();
extern void WmInitWindowSubsystem();

export void WmInit(void) {
    KeInitSpinlock(&winmgr_lock);
    /*
     * The kernel owns the system message box and the hardware hooks; this is
     * where the window manager takes on the jobs that need a user interface.
     */
    KeSetMouseHandler(WmHandleMouseInput);
    KeSetHardErrorHandler(WmHardError);
    WmInitDcCache();
    WmInitWindowSubsystem();
    WmInitWindowClassSubsystem();
    WmInitDesktopWindowSubsystem();    
    WmInitAppSubsystem();
}

export void WmEndPaint(struct window* win, struct dc* dc) {
    (void) win;
    WmReturnDC(dc);
}

/*
 * Returns NULL if there is no DC to paint with (out of memory). Nothing has been changed then, so the
 * window is still dirty and will be painted when there is memory again; callers must not paint or call
 * WmEndPaint.
 */
export struct dc* WmBeginPaint(struct window* win) {
    /* First, so that giving up leaves the dirty region alone. */
    struct dc* dc = WmGetDC();
    if (dc == NULL) {
        return NULL;
    }

    struct region invl_rgn = CdIntersectRegion(win->vis_rgn, win->dirty_rgn);

    struct region new_dirty = CdSubtractRegion(win->dirty_rgn, invl_rgn);
    CdFreeRegion(win->dirty_rgn);
    win->dirty_rgn = new_dirty;

    CdRestrictClipRegion(dc, invl_rgn);
    CdFreeRegion(invl_rgn);

    CdSetTranslation(dc, win->global_offset_cached.x, win->global_offset_cached.y);

    if (!(win->winclass->flags & CS_ALLCLIENT)) {
        KeLogPrintf("Dispatching WM_NCPAINT...\n");
        WmDispatchMessage((struct msg) {
            .type = WM_NCPAINT,
            .p_arg = dc,
            .win = win,
        });
        CdSetTranslation(dc, 0, 0);
        CdRestrictClipRegion(dc, win->client_rgn);
        int cx = win->global_offset_cached.x + (win->local_client_bound.x - win->local_win_bound.x);
        int cy = win->global_offset_cached.y + (win->local_client_bound.y - win->local_win_bound.y);
        CdSetTranslation(dc, cx, cy);
    }
    
    return dc;
}

int WmChangeStyle(struct window* win, int style) {
    return 0; (void) win; (void) style;
}