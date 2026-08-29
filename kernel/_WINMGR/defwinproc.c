#include <obj.h>
#include <heap.h>
#include <log.h>
#include <mutex.h>
#include "winmgr_internal.h"

static void HandleWmPaint(struct window* win) {
    struct dc* dc = WmBeginPaint(win);

    struct brush* b = CdCreateSolidBrush(0xFFFF0000);
    CdPaintRectWithBrush(
        dc, 
        0,
        0,
        win->local_win_bound.w,
        win->local_win_bound.h,
        CdGetStockBrush(STOCK_BRUSH_WHITE)
    );

    CdSetGraphicsObject(dc, b);
    struct region rgn = CdCreateEllipseRegion(50, 50, 500, 500);
    CdPaintRegion(dc, rgn);
    DerefObject(b);
    
    WmEndPaint(win, dc);
}

export int WmDefaultWindowProcedure(struct window* win, struct msg msg) {
    switch (msg.type) {
    case WM_PAINT:
        HandleWmPaint(win);
        return 0;

    default:
        return -1;
    }
}