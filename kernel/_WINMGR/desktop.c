
#include <obj.h>
#include <heap.h>
#include <log.h>
#include <mutex.h>
#include "winmgr_internal.h"

//#define DESKTOP_COLOUR 0xFF00C0F0
#define DESKTOP_COLOUR 0xFF00E0E0


static struct window* desktop_window;
static struct brush* desktop_brush;

export struct window* WmGetDesktop(void) {
    return desktop_window;
}

int DesktopWinProc(struct window* win, struct msg msg) {
    if (win != desktop_window) { 
        return -1;
    }
    switch (msg.type) {
    case WM_PAINT: {
        struct dc* dc = WmBeginPaint(win);
        CdPaintRectWithBrush(dc, 
            win->local_win_bound.x, win->local_win_bound.y,
            win->local_win_bound.w, win->local_win_bound.h,
            desktop_brush
        );
        WmEndPaint(win, dc);
        return 0;
    }
    
    default:
        return WmDefaultWindowProcedure(win, msg);
    }
}

void WmInitDesktopWindowSubsystem() {
    desktop_brush = CdCreateSolidBrush(DESKTOP_COLOUR);
    WmCreateWindowClass(".DESKTOP", DesktopWinProc, CS_ALLCLIENT);
    desktop_window = WmCreateWindow(NULL, ".DESKTOP", (struct rect) {.x = 0, .y = 0, .w = 640, .h = 480}, false);
}
