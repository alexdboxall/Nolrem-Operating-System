
#include <obj.h>
#include <heap.h>
#include <log.h>
#include <mutex.h>
#include <kgfx.h>
#include "winmgr_internal.h"

static struct spinlock winmgr_lock;
static struct window* foreground_window = NULL;

bool anything_happened = false;

export struct window* WmGetForegroundWindow() {
    return foreground_window;
}

export void WmSetForegroundWindow(struct window* win, bool lock) {
    if (lock) WmLock();
    if (win != foreground_window) {
        if (foreground_window != NULL) {
            WmInvalidateWindow(foreground_window, false);
            DerefObject(foreground_window);
        }
        foreground_window = win;
        if (win != NULL) {
            RefObject(win);
            WmInvalidateWindow(win, false);
        }
    }
    if (lock) WmUnlock();
}

static void CleanupWindow(void* _win) {
    struct window* win = _win;
    DerefObject(win->winclass);
    FreeHeap(win);
}

void WmInitWindowSubsystem(void) {
    RegisterUserObjectType(UOBJ_WINDOW, CleanupWindow);
}

void WmLock(void) {
    AcquireSpinlock(&winmgr_lock);
}

void WmUnlock(void) {
    ReleaseSpinlock(&winmgr_lock);
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

bool DoesWindowShowBorder(struct window* win) {
    return !(win->style & WS_MAXIMISED);
}

static void SetInternalWindowBounds(struct window* win, struct rect local_r) {
    bool has_border = DoesWindowShowBorder(win);
    int border_width = has_border ? BORDER_WIDTH : 0;
    int shadow = has_border ? SHADOW_CUT_IN : 0;

    win->local_win_bound = local_r;
    win->local_client_bound = local_r;
    win->local_client_bound.x += border_width;
    win->local_client_bound.y += border_width + TITLEBAR_HEIGHT;
    win->local_client_bound.w = MAX(0, win->local_win_bound.w - border_width * 2 - shadow);
    win->local_client_bound.h = MAX(0, win->local_win_bound.h - border_width * 2 - TITLEBAR_HEIGHT - shadow);
    win->global_offset_cached = WmAccumulateScreenOrigin(win, false);

    win->win_rgn = CdCreateRectRegion(
        win->global_offset_cached.x,
        win->global_offset_cached.y,
        win->local_win_bound.w,
        win->local_win_bound.h
    );
    win->client_rgn = CdCreateRectRegion(
        win->global_offset_cached.x + win->local_client_bound.x - win->local_win_bound.x,
        win->global_offset_cached.y + win->local_client_bound.y - win->local_win_bound.y,
        win->local_client_bound.w,
        win->local_client_bound.h
    );
}

static void ClipOurVisibility(struct window* win) {
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
    SetInternalWindowBounds(win, local_r);
    
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
    ClipOurVisibility(win);

    if (lock) WmUnlock();
}

export int WmCallWinProc(struct window* win, struct msg msg) {
    return win->winclass->proc(win, msg);
}

export struct window* WmCreateWindow(struct window* parent, const char* classname, struct rect local_r, bool lock) {
    struct window_class* wc = WmOpenWindowClass(classname);
    if (wc == NULL) {
        return NULL;
    }
    
    struct window* win = AllocHeap(sizeof(struct window));
    InitUserObject(win, UOBJ_WINDOW);

    if (lock) WmLock();

    anything_happened = true;

    win->restore_pos = local_r;
    win->style = 0;
    win->winclass = wc;
    win->first_child = NULL;
    win->parent = parent;
    win->next_sibling = parent ? parent->first_child : NULL;
    if (parent) {
        parent->first_child = win;
    }

    SetInternalWindowBounds(win, local_r);

    struct region covered_rgn = CdCopyRegion(win->win_rgn);
    if (!CdIsRegionEmpty(covered_rgn)) {
        WmInvalidateExposedRegion(win, &covered_rgn, true);
    }

    CdFreeRegion(covered_rgn);
    win->dirty_rgn = CdCopyRegion(win->win_rgn);
    win->vis_rgn = CdCopyRegion(win->win_rgn);
    ClipOurVisibility(win);

    if (parent == WmGetDesktop()) {
        WmSetForegroundWindow(win, false);
    }
    
    if (lock) WmUnlock();

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
    ClipOurVisibility(win);

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

void WmInit(void) {
    InitSpinlock(&winmgr_lock);
    WmInitSystemMessageBox();
    WmInitDcCache();
    WmInitWindowClassSubsystem();
    WmInitDesktopWindowSubsystem();    
}

export void WmEndPaint(struct window* win, struct dc* dc) {
    (void) win;
    WmReturnDC(dc);
}

export struct dc* WmBeginPaint(struct window* win) {
    struct region invl_rgn = CdIntersectRegion(win->vis_rgn, win->dirty_rgn);

    struct region new_dirty = CdSubtractRegion(win->dirty_rgn, invl_rgn);
    CdFreeRegion(win->dirty_rgn);
    win->dirty_rgn = new_dirty;

    struct dc* dc = WmGetDC();
    CdRestrictClipRegion(dc, invl_rgn);
    CdFreeRegion(invl_rgn);

    CdSetTranslation(dc, win->global_offset_cached.x, win->global_offset_cached.y);

    if (!(win->winclass->flags & CS_ALLCLIENT)) {
        WmCallWinProc(win, (struct msg) {
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
