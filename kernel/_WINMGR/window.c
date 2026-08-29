
#include <obj.h>
#include <heap.h>
#include <log.h>
#include <mutex.h>
#include "winmgr_internal.h"

#define BORDER_WIDTH    3
#define SHADOW_CUT_IN   2
#define TITLEBAR_HEIGHT 20
#define TITLEBAR_COL_1  0xFF000080
#define TITLEBAR_COL_2  0xFF00CAFF

static struct spinlock winmgr_lock;

static struct window* desktop_window;

static void CleanupWindow(void* _win) {
    struct window* win = _win;
    FreeHeap(win);
}

void WmInitWindowSubsystem(void) {
    RegisterUserObjectType(UOBJ_WINDOW, CleanupWindow);
}

static void WmLock(void) {
    AcquireSpinlock(&winmgr_lock);
}

static void WmUnlock(void) {
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

export void WmInvalidateRegion(struct window* win, struct region rgn, bool lock) {
    if (lock) WmLock();
    CdUnionRegionInPlace(&win->dirty_rgn, rgn);
    if (lock) WmUnlock();
}

export void WmInvalidateWindow(struct window* win, bool lock) {
    if (lock) WmLock();
    CdFreeRegion(win->dirty_rgn);
    win->dirty_rgn = CdEverythingRegion();
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

static void WmInvalidateExposedRegion(struct window* win, struct region* exposed_rgn, bool actually_cover) {
    struct window* bro = win->next_sibling;
    while (bro) {
        bool now_empty = WmInvalidateExposedRegionOnWindow(bro, exposed_rgn, actually_cover);
        if (now_empty) {
            return;
        }
        bro = bro->next_sibling;
    }
    if (win->parent) {
        WmInvalidateExposedRegionOnWindow(win->parent, exposed_rgn, actually_cover);
    }
}

static void SetInternalWindowBounds(struct window* win, struct rect local_r) {
    win->local_win_bound = local_r;
    win->local_client_bound = local_r;
    win->local_client_bound.x += BORDER_WIDTH;
    win->local_client_bound.y += BORDER_WIDTH + TITLEBAR_HEIGHT;
    win->local_client_bound.w = MAX(0, win->local_win_bound.w - BORDER_WIDTH * 2 - SHADOW_CUT_IN);
    win->local_client_bound.h = MAX(0, win->local_win_bound.h - BORDER_WIDTH * 2 - TITLEBAR_HEIGHT - SHADOW_CUT_IN);
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

static void ClipVisibilityAgainstEarlierSiblings(struct window* win) {
    struct window* big_bro = win->parent == NULL ? NULL : win->parent->first_child;
    while (big_bro != NULL && big_bro != win) {
        CdSubtractRegionInPlace(&win->vis_rgn, big_bro->win_rgn);
        if (CdIsRegionEmpty(win->vis_rgn)) {
            break;
        }
        big_bro = big_bro->next_sibling;
    }
}

export void WmChangePosition(struct window* win, struct rect local_r, bool lock) {
    if (lock) WmLock();

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
    CdFreeRegion(old_win_bounds);    

    /* Make the window dirty. */
    CdFreeRegion(win->dirty_rgn);
    win->dirty_rgn = CdCopyRegion(win->win_rgn);

    /* Figure out our own visible region. */
    CdFreeRegion(win->vis_rgn);
    win->vis_rgn = CdCopyRegion(win->win_rgn);
    ClipVisibilityAgainstEarlierSiblings(win);

    if (lock) WmUnlock();
}

struct window* WmCreateWindow(struct window* parent, struct rect local_r, bool lock) {
    struct window* win = AllocHeap(sizeof(struct window));
    InitUserObject(win, UOBJ_WINDOW);

    if (lock) WmLock();

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
    ClipVisibilityAgainstEarlierSiblings(win);

    if (lock) WmUnlock();

    return win;
}

export void WmRaiseToTop(struct window* win, bool lock) {
    if (lock) WmLock();

    if (win->parent != NULL && win->parent->first_child != win) {
        /* Unlink win, then relink it at the front of its parent's child list. */
        struct window* prev = win->parent->first_child;
        while (prev->next_sibling != win) {
            prev = prev->next_sibling;
        }
        prev->next_sibling = win->next_sibling;
        win->next_sibling = win->parent->first_child;
        win->parent->first_child = win;

        /* win is now topmost among its siblings, so its own vis_rgn can only grow -
           anything previously hidden behind a former big_bro is revealed. */
        struct region old_vis = win->vis_rgn;
        win->vis_rgn = CdCopyRegion(win->win_rgn);
        struct region newly_visible = CdSubtractRegion(win->vis_rgn, old_vis);
        if (!CdIsRegionEmpty(newly_visible)) {
            CdUnionRegionInPlace(&win->dirty_rgn, newly_visible);
        }
        CdFreeRegion(newly_visible);
        CdFreeRegion(old_vis);

        /* Whatever win now sits in front of needs win's shape punched out of its vis_rgn.
           Reuses the same pass WmChangePosition/WmCreateWindow use for "covered". For
           siblings that were already behind win, this is a no-op - their vis_rgn already
           excludes win's shape from when win was first created/positioned there. */
        struct region covered_rgn = CdCopyRegion(win->win_rgn);
        WmInvalidateExposedRegion(win, &covered_rgn, true);
        CdFreeRegion(covered_rgn);
    }

    if (lock) WmUnlock();
}

export struct window* WmGetDesktop(void) {
    return desktop_window;
}

export void WmDefaultNonClientPaint(struct dc* dc, struct window* win) {
    struct brush* blue = CdCreateSolidBrush(0xFF000080);
    CdPaintRectWithBrush(dc, BORDER_WIDTH, BORDER_WIDTH, win->local_client_bound.w, TITLEBAR_HEIGHT, blue);
    DerefObject(blue);

    CdPaintRectWithBrush(dc, 
        0,
        0,
        win->local_win_bound.w - SHADOW_CUT_IN,
        BORDER_WIDTH,
        CdGetStockBrush(STOCK_BRUSH_SYSTEM)
    );
    CdPaintRectWithBrush(dc, 
        0,
        win->local_win_bound.h - BORDER_WIDTH - SHADOW_CUT_IN,
        win->local_win_bound.w - SHADOW_CUT_IN,
        BORDER_WIDTH,
        CdGetStockBrush(STOCK_BRUSH_SYSTEM)
    );
    CdPaintRectWithBrush(dc, 
        0,
        win->local_win_bound.h - SHADOW_CUT_IN,
        win->local_win_bound.w,
        BORDER_WIDTH,
        CdGetStockBrush(STOCK_BRUSH_BLACK)
    );
    CdPaintRectWithBrush(dc, 
        0,
        0,
        BORDER_WIDTH,
        win->local_win_bound.h - SHADOW_CUT_IN - BORDER_WIDTH,
        CdGetStockBrush(STOCK_BRUSH_SYSTEM)
    );
    CdPaintRectWithBrush(dc, 
        win->local_win_bound.w - SHADOW_CUT_IN - BORDER_WIDTH,
        0,
        BORDER_WIDTH,
        win->local_win_bound.h - SHADOW_CUT_IN,
        CdGetStockBrush(STOCK_BRUSH_SYSTEM)
    );
    CdPaintRectWithBrush(dc, 
        win->local_win_bound.w - SHADOW_CUT_IN,
        0,
        BORDER_WIDTH,
        win->local_win_bound.h,
        CdGetStockBrush(STOCK_BRUSH_BLACK)
    );
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

// ========================= MOVE TO OWN FILE =========================
#define DC_CACHE_SIZE   8
struct dc_cache_entry {
    struct dc* dc;
    bool allocated;
};

static struct mutex* dc_cache_mtx;
static struct dc_cache_entry dc_cache[DC_CACHE_SIZE];

export struct dc* WmGetDC(void) {
    int res = AcquireMutex(dc_cache_mtx, TIMEOUT_INFINITE);
    if (res != 0) {
        return NULL;
    }

    for (int i = 0 ; i < DC_CACHE_SIZE; ++i) {
        if (!dc_cache[i].allocated) {
            dc_cache[i].allocated = true;
            struct dc* dc = dc_cache[i].dc;
            ReleaseMutex(dc_cache_mtx);
            return dc;
        }
    }

    ReleaseMutex(dc_cache_mtx);
    return CdCreateDc();
}

export int WmReturnDC(struct dc* dc) {
    int res = AcquireMutex(dc_cache_mtx, TIMEOUT_INFINITE);
    if (res != 0) {
        return res;
    }

    for (int i = 0 ; i < DC_CACHE_SIZE; ++i) {
        if (dc_cache[i].dc == dc) {
            dc_cache[i].allocated = false;
            // We reset the DC on return, instead of Get(), because this will
            // often allow e.g. a brush or pen set by the user of the DC to be
            // properly released and cleaned up (instead of waiting for the 
            // next Get() for it to be cleaned up).
            CdResetDC(dc);
            ReleaseMutex(dc_cache_mtx);
            return 0;
        }
    }

    ReleaseMutex(dc_cache_mtx);
    DerefObject(dc);
    return 0;
}

void WmInitDcCache(void) {
    for (int i = 0; i < DC_CACHE_SIZE; ++i) {
        dc_cache[i].dc = CdCreateDc();
        dc_cache[i].allocated = false;
    }
    dc_cache_mtx = CreateMutex();
}

// ========================= MOVE TO OWN FILE =========================

void WmInit(void) {
    InitSpinlock(&winmgr_lock);
    WmInitDcCache();
    desktop_window = WmCreateWindow(NULL, (struct rect) {.x = 0, .y = 0, .w = 640, .h = 480}, false);
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
    CdTranslateCoordinates(dc, win->global_offset_cached.x, win->global_offset_cached.y);
    WmDefaultNonClientPaint(dc, win);
    CdTranslateCoordinates(dc, -win->global_offset_cached.x, -win->global_offset_cached.y);
    CdRestrictClipRegion(dc, win->client_rgn);
    int cx = win->global_offset_cached.x - win->local_win_bound.x + win->local_client_bound.x;
    int cy = win->global_offset_cached.y - win->local_win_bound.y + win->local_client_bound.y;
    CdTranslateCoordinates(dc, cx, cy);
    return dc;
}
