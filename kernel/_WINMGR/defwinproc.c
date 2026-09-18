#include <obj.h>
#include <heap.h>
#include <log.h>
#include <mutex.h>
#include <kgfx.h>
#include "winmgr_internal.h"

#define TITLEBAR_COL_1              0xFF000080
#define TITLEBAR_COL_2              0xFF00CAFF
#define TITLEBAR_INACTIVE_COL_1     0xFF404040
#define TITLEBAR_INACTIVE_COL_2     0xFF808080

static void HandleWmPaint(struct window* win) {
    struct dc* dc = WmBeginPaint(win);

    CdPaintRectWithBrush(
        dc, 
        0,
        0,
        win->local_win_bound.w,
        win->local_win_bound.h,
        CdGetStockBrush(STOCK_BRUSH_WHITE)
    );
    
    WmEndPaint(win, dc);
}

static bool UseGradientTitlebar(struct dc* dc) {
    struct graphics_driver* drv = GetOutputDriver(dc);
    struct graphics_capabilities caps = drv->get_capabilities(drv);
    return caps.bits_per_pixel >= 15;
}

static void DefaultNonClientPaint(struct dc* dc, struct window* win) {
    int width = win->local_client_bound.w;

    bool foreground = WmGetForegroundWindow() == win;

    uint32_t col1 = foreground ? TITLEBAR_COL_1 : TITLEBAR_INACTIVE_COL_1;
    uint32_t col2 = foreground ? TITLEBAR_COL_2 : TITLEBAR_INACTIVE_COL_2;

    struct brush* blue = CdCreateSolidBrush(col1);

    if (UseGradientTitlebar(dc)) {
        int initial_part = width / 3;
        int remaining_part = width - initial_part;
        int gradient_part = remaining_part / 4 * 3;
        int end_part = remaining_part - gradient_part;
        CdPaintRectWithBrush(
            dc, BORDER_WIDTH, BORDER_WIDTH, initial_part, TITLEBAR_HEIGHT, blue
        );
        CdPaintGradientRectHz(
            dc, BORDER_WIDTH + initial_part, BORDER_WIDTH, gradient_part, TITLEBAR_HEIGHT,
            col1, col2
        );
        CdSetBrushColour(blue, col2);
        CdPaintRectWithBrush(
            dc, BORDER_WIDTH + initial_part + gradient_part, BORDER_WIDTH, end_part, TITLEBAR_HEIGHT, blue
        );
    } else {
        CdPaintRectWithBrush(
            dc, BORDER_WIDTH, BORDER_WIDTH, width, TITLEBAR_HEIGHT, blue
        );
    }
    
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


/* Point relative to the top-left of the window. */
static int HandleHitTest(struct window* win, struct point p) {
    int retv = 0;
    if (p.x < BORDER_WIDTH) retv |= HIT_LEFT_BORDER;
    if (p.y < BORDER_WIDTH) retv |= HIT_TOP_BORDER;
    if (p.x >= win->local_win_bound.w - BORDER_WIDTH) retv |= HIT_RIGHT_BORDER;
    if (p.y >= win->local_win_bound.h - BORDER_WIDTH) retv |= HIT_BOTTOM_BORDER;
    if (p.y >= SHADOW_CUT_IN && p.y < TITLEBAR_HEIGHT + SHADOW_CUT_IN) retv |= HIT_TITLEBAR;
    if (WmIsPointInRect(p.x, p.y, win->local_client_bound)) {
        retv |= HIT_CLIENT;
    } else {
        retv |= HIT_NONCLIENT;
    }

    /* Make the corner(s) bigger. */
    if (p.x >= win->local_win_bound.w - CORNER_WIDTH) {
        //if (p.y < CORNER_WIDTH)                          retv |= HIT_RIGHT_BORDER | HIT_TOP_BORDER;
        if (p.y > win->local_win_bound.h - CORNER_WIDTH) retv |= HIT_RIGHT_BORDER | HIT_BOTTOM_BORDER;

    } else if (p.x < CORNER_WIDTH) {
        //if (p.y < CORNER_WIDTH)                          retv |= HIT_LEFT_BORDER | HIT_TOP_BORDER;
        //if (p.y > win->local_win_bound.h - CORNER_WIDTH) retv |= HIT_LEFT_BORDER | HIT_BOTTOM_BORDER;
    }

    return retv;
}

static int HandleToplevelMousedown(struct window* win, struct msg msg) {
    struct point p = msg.point_arg1;
    WmLock();
    p.x -= win->global_offset_cached.x;
    p.y -= win->global_offset_cached.y;
    WmRaiseToTop(win, false);
    WmSetForegroundWindow(win, false);
    WmUnlock();
    int retv = WmCallWinProc(win, (struct msg) {
        .win = win,
        .type = WM_HITTEST,
        .point_arg1 = p
    });
    if (retv & (HIT_BOTTOM_BORDER | HIT_LEFT_BORDER | HIT_RIGHT_BORDER | HIT_TOP_BORDER)) {
        WmStartResizingWindow(win, retv); 
    } else if (retv & HIT_TITLEBAR) {
        WmStartDraggingWindow(win);
    }
    return retv;
}

export int WmDefaultWindowProcedure(struct window* win, struct msg msg) {
    switch (msg.type) {
    case WM_PAINT:
        HandleWmPaint(win);
        return 0;

    case WM_NCPAINT:
        DefaultNonClientPaint((struct dc*) msg.p_arg, win);
        return 0;

    case WM_HITTEST:
        return HandleHitTest(win, msg.point_arg1);

    case WM_TOPLEVEL_MOUSEDOWN:
        return HandleToplevelMousedown(win, msg);

    default:
        return -1;
    }
}