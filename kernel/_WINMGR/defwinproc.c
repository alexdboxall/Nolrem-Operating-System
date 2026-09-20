#include <obj.h>
#include <heap.h>
#include <log.h>
#include <mutex.h>
#include <kgfx.h>
#include <timer.h>
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
    bool has_border = DoesWindowShowBorder(win);
    int border_width = has_border ? BORDER_WIDTH : 0;
    int shadow = has_border ? SHADOW_CUT_IN : 0;

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
            dc, border_width, border_width, initial_part, TITLEBAR_HEIGHT, blue
        );
        CdPaintGradientRectHz(
            dc, border_width + initial_part, border_width, gradient_part, TITLEBAR_HEIGHT,
            col1, col2
        );
        CdSetBrushColour(blue, col2);
        CdPaintRectWithBrush(
            dc, border_width + initial_part + gradient_part, border_width, end_part, TITLEBAR_HEIGHT, blue
        );
    } else {
        CdPaintRectWithBrush(
            dc, border_width, border_width, width, TITLEBAR_HEIGHT, blue
        );
    }
    
    DerefObject(blue);

    CdPaintRectWithBrush(dc, 
        0,
        0,
        win->local_win_bound.w - shadow,
        border_width,
        CdGetStockBrush(STOCK_BRUSH_SYSTEM)
    );
    CdPaintRectWithBrush(dc, 
        0,
        win->local_win_bound.h - border_width - shadow,
        win->local_win_bound.w - shadow,
        border_width,
        CdGetStockBrush(STOCK_BRUSH_SYSTEM)
    );
    CdPaintRectWithBrush(dc, 
        0,
        win->local_win_bound.h - shadow,
        win->local_win_bound.w,
        border_width,
        CdGetStockBrush(STOCK_BRUSH_BLACK)
    );
    CdPaintRectWithBrush(dc, 
        0,
        0,
        border_width,
        win->local_win_bound.h - shadow - border_width,
        CdGetStockBrush(STOCK_BRUSH_SYSTEM)
    );    
    CdPaintRectWithBrush(dc, 
        win->local_win_bound.w - shadow - border_width,
        0,
        border_width,
        win->local_win_bound.h - shadow,
        CdGetStockBrush(STOCK_BRUSH_SYSTEM)
    );
    CdPaintRectWithBrush(dc, 
        win->local_win_bound.w - shadow,
        0,
        border_width,
        win->local_win_bound.h,
        CdGetStockBrush(STOCK_BRUSH_BLACK)
    );
}


/* Point relative to the top-left of the window. */
static int HandleHitTest(struct window* win, struct point p) {
    bool has_border = DoesWindowShowBorder(win);
    int shadow = has_border ? SHADOW_CUT_IN : 0;

    int retv = 0;
    if (p.y >= shadow && p.y < TITLEBAR_HEIGHT + shadow) retv |= HIT_TITLEBAR;
    if (WmIsPointInRect(p.x, p.y, win->local_client_bound)) {
        retv |= HIT_CLIENT;
    } else {
        retv |= HIT_NONCLIENT;
    }

    if (!has_border) {
        return retv;
    }

    if (p.x < BORDER_WIDTH) retv |= HIT_LEFT_BORDER;
    if (p.y < BORDER_WIDTH) retv |= HIT_TOP_BORDER;
    if (p.x >= win->local_win_bound.w - BORDER_WIDTH) retv |= HIT_RIGHT_BORDER;
    if (p.y >= win->local_win_bound.h - BORDER_WIDTH) retv |= HIT_BOTTOM_BORDER;

    /* Make the corner(s) bigger. */
    if (p.x >= win->local_win_bound.w - CORNER_WIDTH) {
        if (p.y < CORNER_WIDTH)                          retv |= HIT_RIGHT_BORDER | HIT_TOP_BORDER;
        if (p.y > win->local_win_bound.h - CORNER_WIDTH) retv |= HIT_RIGHT_BORDER | HIT_BOTTOM_BORDER;

    } else if (p.x < CORNER_WIDTH) {
        if (p.y < CORNER_WIDTH)                          retv |= HIT_LEFT_BORDER | HIT_TOP_BORDER;
        if (p.y > win->local_win_bound.h - CORNER_WIDTH) retv |= HIT_LEFT_BORDER | HIT_BOTTOM_BORDER;
    }

    return retv;
}

static int HandleToplevelMousedown(struct window* win, struct msg msg) {
    static uint64_t prev_time = 0;
    static struct point prev_pt;
    uint64_t time = GetTimeSinceBoot();
    uint64_t time_delta = time - prev_time;
    prev_time = time;

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
    WmLock();
    if (retv & (HIT_BOTTOM_BORDER | HIT_LEFT_BORDER | HIT_RIGHT_BORDER | HIT_TOP_BORDER)) {
        WmStartResizingWindow(win, retv); 

    } else if (retv & HIT_TITLEBAR) {
        bool maxed = win->style & WS_MAXIMISED;
        bool double_click = time_delta < DOUBLE_CLICK_MS * 1000 * 1000
                            && ABS(p.x - prev_pt.x) < 3
                            && ABS(p.y - prev_pt.y) < 3;

        if (maxed) {
            if (double_click) {
                win->style &= ~WS_MAXIMISED;
                WmChangePosition(win, win->restore_pos, false);
            }

        } else {
            if (double_click) {
                win->restore_pos = win->local_win_bound;
                win->style |= WS_MAXIMISED;
                WmChangePosition(win, WmGetDesktop()->local_win_bound, false);
            } else {
                WmStartDraggingWindow(win);
            }
        }
    }
    prev_pt = p;
    WmUnlock();
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