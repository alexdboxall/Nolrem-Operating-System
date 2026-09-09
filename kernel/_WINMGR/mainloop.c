
#include <obj.h>
#include <heap.h>
#include <log.h>
#include <mutex.h>
#include <msgbox.h>
#include <spinlock.h>
#include <arch.h>
#include <vmm.h>
#include <thread.h>
#include <stdatomic.h>
#include "winmgr_internal.h"

#define SYSTEM_MSGBOX_SIZE      128

static struct msgbox* sys_mbox = NULL;

export struct msgbox* WmGetSystemMessageBox(void) {
    return sys_mbox;
}

export void WmInitSystemMessageBox(void) {
    sys_mbox = CreateMessageBox(sizeof(struct msg), SYSTEM_MSGBOX_SIZE);
}

bool wm_mainloop_started = false;

struct point prev_mouse_pt;
int mouse_x = 0;
int mouse_y = 0;
int prev_mouse_buttons = 0;
int mouse_buttons = 0;

static struct window* dragging_win = NULL;
static int drag_mouse_start_x = 0;
static int drag_mouse_start_y = 0;
static struct rect drag_win_og_bounds;

#define MOUSE_BUTTON_LEFT           1

static void DrawInvFrame(struct rect pos) {
    const int BORDER = 3;
    struct dc* dc = WmGetDC();
    ActualInvertRect(dc, pos.x, pos.y, pos.x + pos.w, pos.y + BORDER, true);
    ActualInvertRect(dc, pos.x, pos.y + pos.h - BORDER, pos.x + pos.w, pos.y + pos.h, true);
    ActualInvertRect(dc, pos.x, pos.y + BORDER, pos.x + BORDER, pos.y + pos.h - BORDER, true);
    ActualInvertRect(dc, pos.x + pos.w - BORDER, pos.y + BORDER, pos.x + pos.w, pos.y + pos.h - BORDER, true);
    WmReturnDC(dc);
}

// Needs ref count already added from WmGetToplevelAtPoint.
static void WmStartDraggingWindow(struct window* win) {
    drag_mouse_start_x = mouse_x;
    drag_mouse_start_y = mouse_y;
    drag_win_og_bounds = win->local_client_bound;
    dragging_win = win;

    WmSetForegroundWindow(win, true);
    WmRaiseToTop(win, true);
}

static void WmStopDraggingWindow(void) {
    if (dragging_win != NULL) {
        struct rect r = dragging_win->local_win_bound;
        r.x += mouse_x - drag_mouse_start_x;
        r.y += mouse_y - drag_mouse_start_y;
        WmChangePosition(dragging_win, r, true); 
        DerefObject(dragging_win);
        dragging_win = NULL;
    }
}

static bool HandleMouse(int mx, int my, int click_bits) {
    mouse_x = mx;
    mouse_y = my;

    struct point mouse_pt = (struct point) {.x = mouse_x, .y = mouse_y};
    bool moved = prev_mouse_pt.x != mouse_x || prev_mouse_pt.y != mouse_y;

    if (moved) {
        CdRemoveMouse(prev_mouse_pt.x, prev_mouse_pt.y);
        if (dragging_win) {
            struct rect r = dragging_win->local_win_bound;
            r.x += prev_mouse_pt.x - drag_mouse_start_x;
            r.y += prev_mouse_pt.y - drag_mouse_start_y;
            DrawInvFrame(r);

            r = dragging_win->local_win_bound;
            r.x += mouse_x - drag_mouse_start_x;
            r.y += mouse_y - drag_mouse_start_y;
            DrawInvFrame(r);
        }
        CdDrawMouse(mouse_x, mouse_y);
    }

    prev_mouse_buttons = mouse_buttons;
    mouse_buttons = click_bits;

    // Mouse down
    if ((mouse_buttons & MOUSE_BUTTON_LEFT) && !(prev_mouse_buttons & MOUSE_BUTTON_LEFT)) {
        struct window* win = WmGetToplevelAtPoint(mouse_x, mouse_y, true);
        if (win != NULL) {
            // TODO: make this something that involves sending a message to the window
            //       so that it can properly paint itself at toplevel before being dragged
            WmStartDraggingWindow(win);
        }
    }
    
    bool retv = false;

    // Mouse up
    if (!(mouse_buttons & MOUSE_BUTTON_LEFT) && (prev_mouse_buttons & MOUSE_BUTTON_LEFT)) {
        if (dragging_win != NULL) {
            WmStopDraggingWindow();
            retv = true;
        }
    }

    prev_mouse_pt = mouse_pt;
    return retv;
}

struct window* win;
struct window* win2;
struct window* win3;

static void ProcessMessage(struct msg msg) {
    switch (msg.type) {
    case SYSMSG_LOWMEMORY:
        LogPrintf("Low memory...\n");
        DiscardPage();
        break;

    case SYSMSG_MOUSEEVENT:
        bool up = HandleMouse(msg.rect_arg.x, msg.rect_arg.y, msg.i_arg);
        LogPrintf("Handling mouse event...\n");
        if (up) {
            WmCallWinProc(WmGetDesktop(), (struct msg) {
                .type = WM_PAINT
            });
            WmCallWinProc(win, (struct msg) {
                .type = WM_PAINT
            });
            WmCallWinProc(win2, (struct msg) {
                .type = WM_PAINT
            });
            WmCallWinProc(win3, (struct msg) {
                .type = WM_PAINT
            });
        }
        break;
    }
}

_Noreturn void WmMainloop(void) {
    SetThreadPriority(GetCurrentThread(), PRIORITY_MAX);

    struct msg msg;
    wm_mainloop_started = true;

    struct rect w1pos = (struct rect) {
        .x = 75, .y = 75, .w = 300, .h = 350
    };
    struct rect w2pos = (struct rect) {
        .x = 175, .y = 175, .w = 450, .h = 150
    };
    struct rect w3pos = (struct rect) {
        .x = 50, .y = 50, .w = 50, .h = 50
    };

    win = WmCreateWindow(WmGetDesktop(), NULL, w1pos, true);
    win2 = WmCreateWindow(WmGetDesktop(), NULL, w2pos, true);
    win3 = WmCreateWindow(win2, NULL, w3pos, true);
    
    WmCallWinProc(WmGetDesktop(), (struct msg) {
        .type = WM_PAINT
    });
    WmCallWinProc(win, (struct msg) {
        .type = WM_PAINT
    });
    WmCallWinProc(win2, (struct msg) {
        .type = WM_PAINT
    });
    (void) win3;
    /*WmCallWinProc(win3, (struct msg) {
        .type = WM_PAINT
    });*/
           
    while (true) {
        KeGetMessage(sys_mbox, &msg, TIMEOUT_INFINITE);
        ProcessMessage(msg);
    }
}