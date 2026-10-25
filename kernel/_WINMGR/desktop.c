
#include <obj.h>
#include <heap.h>
#include <log.h>
#include <mutex.h>
#include "winmgr_internal.h"
#include "wattle.h"


#define DESKTOP_COLOUR 0xFF00C9FF

static struct window* desktop_window;
static struct brush* desktop_brush;
static struct app* desktop_app;

export struct window* WmGetDesktop(void) {
    return desktop_window;
}

struct compat_bitmap* desktop_bitmap = NULL;

int DesktopWinProc(struct window* win, struct msg msg) {
    if (win != desktop_window) { 
        return -1;
    }
    switch (msg.type) {
    case WM_SETCURSOR:
        CdSetStandardCursor(CURSOR_NORMAL);
        return 0;

    case WM_PAINT: {
        struct dc* dc = WmBeginPaint(win);
        if (desktop_bitmap == NULL) {
            desktop_bitmap = CdCreateCompatibleBitmapStretched(dc, wattle_bmp, 640, 480);
        }
        CdPaintBitmap(dc, desktop_bitmap, (struct point) {.x = 0, .y = 0});
        WmEndPaint(win, dc);
        return 0;
    }
    
    default:
        return WmDefaultWindowProcedure(win, msg);
    }
}

void WmInitDesktopWindowSubsystem() {
    desktop_app = WmCreateApp();
    desktop_brush = CdCreateSolidBrush(DESKTOP_COLOUR);
    WmCreateWindowClass(".DESKTOP", DesktopWinProc, CS_ALLCLIENT);
    desktop_window = WmCreateWindow(desktop_app, NULL, ".DESKTOP", (struct rect) {.x = 0, .y = 0, .w = 640, .h = 480}, false);
}
