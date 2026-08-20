
#include <obj.h>
#include <heap.h>
#include "winmgr_internal.h"

#define BORDER_WIDTH    3
#define SHADOW_CUT_IN   2
#define TITLEBAR_HEIGHT 20
#define TITLEBAR_COL_1  0xFF000080
#define TITLEBAR_COL_2  0xFF00CAFF

struct window {
    // LOCAL COORDS
    struct rect local_win_bound;
    struct rect local_client_bound;

    // GLOBAL
    struct region win_rgn;
    struct region client_rgn;
    struct region vis_rgn;
    struct region dirty_rgn;
    struct point global_offset_cached;

    struct window* next_sibling;
    struct window* first_child;
    struct window* parent;
};

static void CleanupWindow(void* _win) {
    struct window* win = _win;
    FreeHeap(win);
}

void WmInitWindowSubsystem(void) {
    RegisterUserObjectType(UOBJ_WINDOW, CleanupWindow);
}

static struct point WmAccumulateScreenOrigin(struct window* win) {
    int x = win->local_win_bound.x;
    int y = win->local_win_bound.y;
    for (struct window* p = win->parent; p != NULL; p = p->parent) {
        x += p->local_win_bound.x;
        y += p->local_win_bound.y;
    }
    return (struct point) {.x = x, .y = y};
}

struct window* WmCreateWindow(struct rect local_r) {
    struct window* win = AllocHeap(sizeof(struct window));
    InitUserObject(win, UOBJ_WINDOW);
    win->local_win_bound = local_r;
    win->local_client_bound = local_r;

    win->local_client_bound.x += BORDER_WIDTH;
    win->local_client_bound.y += BORDER_WIDTH + TITLEBAR_HEIGHT;
    win->local_client_bound.w = MAX(0, win->local_win_bound.w - BORDER_WIDTH * 2 - SHADOW_CUT_IN);
    win->local_client_bound.h = MAX(0, win->local_win_bound.h - BORDER_WIDTH * 2 - TITLEBAR_HEIGHT - SHADOW_CUT_IN);

    win->first_child = NULL;
    win->next_sibling = NULL;
    win->parent = NULL;

    win->global_offset_cached = WmAccumulateScreenOrigin(win);

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
    win->vis_rgn = CdCopyRegion(win->win_rgn);
    win->dirty_rgn = CdCopyRegion(win->win_rgn);

    return win;
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

void WmDummyDrawClientArea(struct dc* dc, struct window* win) {
    CdPaintRectWithBrush(
        dc, 
        0,
        0,
        win->local_win_bound.w,
        win->local_win_bound.h,
        CdGetStockBrush(STOCK_BRUSH_WHITE)
    );

    struct region rgn = CdCreateEllipseRegion(50, 50, 200, 200);
    CdPaintRegion(dc, rgn);
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

export struct dc* WmGetDC(void) {
    return CdCreateDc();    // TODO: maintain a local cache
}

export void WmEndPaint(struct window* win, struct dc* dc) {
    (void) win;
    DerefObject(dc);
}

#include <log.h>

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
    CdTranslateCoordinates(dc, win->local_client_bound.x, win->local_client_bound.y);
    WmDummyDrawClientArea(dc, win);
    return dc;
}



void WmDrawWindow(struct dc* dc, struct window* win) {
    (void) dc;
    (void) win;
}
