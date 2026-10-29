
#include <obj.h>
#include <heap.h>
#include <log.h>
#include <phys.h>
#include <mutex.h>
#include <omf.h>
#include "winmgr_internal.h"

#define DESKTOP_COLOUR 0xFF00C9FF

static struct window* desktop_window;
static struct brush* desktop_brush;
static struct app* desktop_app;

export struct window* WmGetDesktop(void) {
    return desktop_window;
}

struct compat_bitmap* desktop_bitmap = NULL;
static int oom = 0;
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
            struct open_mapped_file omf;
            OpenAndMapFile("drv0:/SYSTEM/WATTLE.BMP", &omf);
            desktop_bitmap = CdCreateCompatibleBitmapStretched(dc, omf.f, 640, 480);
            OmfClose(&omf);
        }
        if (IsOutOfMemory() || oom > 0 || desktop_bitmap == NULL) {
            if (IsOutOfMemory() && oom == 0) {
                WmInvalidateWindow(desktop_window, true);
            }
            oom = 256;
            CdPaintRectWithBrush(dc, 0, 0, win->local_win_bound.w, win->local_win_bound.h, desktop_brush);
        } else {
            if (oom > 0 && !IsMemoryLow()) {
                --oom;
                if (oom == 0) {
                    WmInvalidateWindow(desktop_window, true);
                }
            }
            CdPaintBitmap(dc, desktop_bitmap, (struct point) {.x = 0, .y = 0});
        }
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
