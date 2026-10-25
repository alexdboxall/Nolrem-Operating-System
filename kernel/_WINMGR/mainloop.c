
#include <obj.h>
#include <heap.h>
#include <log.h>
#include <mutex.h>
#include <msgbox.h>
#include <vfs.h>
#include <timer.h>
#include <string.h>
#include <spinlock.h>
#include <arch.h>
#include <fcntl.h>
#include <vmm.h>
#include <thread.h>
#include <scheduler.h>
#include <kgfx.h>
#include <phys.h>
#include <stdatomic.h>
#include "winmgr_internal.h"

extern void PurgeScaledGlyphCacheIfNeeded();

#define SYSTEM_MSGBOX_SIZE      8

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
static struct window* resizing_win = NULL;
static int resize_hit_test_results = 0;
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


/* Reasonable floor so a border can't be dragged past its opposite edge. Mirrors
   the client-size clamp in SetInternalWindowBounds. */
#define MIN_WINDOW_W (BORDER_WIDTH * 2 + SHADOW_CUT_IN + 20)
#define MIN_WINDOW_H (BORDER_WIDTH * 2 + TITLEBAR_HEIGHT + SHADOW_CUT_IN + 20)

/* Applies a resize identified by hit_test_results (see HIT_*_BORDER) to `base`,
   given how far the mouse has moved (dx, dy) since the resize started. If the
   drag would shrink a dimension past the minimum, that dimension is clamped
   without letting the fixed (non-dragged) edge move. */
static struct rect WmApplyResizeDelta(struct rect base, int dx, int dy, int hit_test_results) {
    struct rect r = base;

    if (hit_test_results & HIT_LEFT_BORDER) {
        r.x += dx;
        r.w -= dx;
    }
    if (hit_test_results & HIT_RIGHT_BORDER) {
        r.w += dx;
    }
    if (hit_test_results & HIT_TOP_BORDER) {
        r.y += dy;
        r.h -= dy;
    }
    if (hit_test_results & HIT_BOTTOM_BORDER) {
        r.h += dy;
    }

    if (r.w < MIN_WINDOW_W) {
        if (hit_test_results & HIT_LEFT_BORDER) {
            r.x -= (MIN_WINDOW_W - r.w);
        }
        r.w = MIN_WINDOW_W;
    }
    if (r.h < MIN_WINDOW_H) {
        if (hit_test_results & HIT_TOP_BORDER) {
            r.y -= (MIN_WINDOW_H - r.h);
        }
        r.h = MIN_WINDOW_H;
    }

    return r;
}

void WmStartDraggingWindow(struct window* win) {
    RefObject(win);
    drag_mouse_start_x = mouse_x;
    drag_mouse_start_y = mouse_y;
    drag_win_og_bounds = win->local_client_bound;
    dragging_win = win;
}

void WmStartResizingWindow(struct window* win, int hit_test_results) {
    RefObject(win);
    drag_mouse_start_x = mouse_x;
    drag_mouse_start_y = mouse_y;
    drag_win_og_bounds = win->local_client_bound;
    resizing_win = win;   

    resize_hit_test_results = hit_test_results; 
}

static void WmStopResizingWindow(void) {
    if (resizing_win != NULL) {
        struct rect r = resizing_win->local_win_bound;
        
        r = WmApplyResizeDelta(r, mouse_x - drag_mouse_start_x, mouse_y - drag_mouse_start_y,
                                resize_hit_test_results);
        
        WmChangePosition(resizing_win, r, true); 
        DerefObject(resizing_win);
        resizing_win = NULL;
    }
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
        if (resizing_win) {
            struct rect r = WmApplyResizeDelta(resizing_win->local_win_bound,
                prev_mouse_pt.x - drag_mouse_start_x, prev_mouse_pt.y - drag_mouse_start_y,
                resize_hit_test_results);
            DrawInvFrame(r);

            r = WmApplyResizeDelta(resizing_win->local_win_bound,
                mouse_x - drag_mouse_start_x, mouse_y - drag_mouse_start_y,
                resize_hit_test_results);
            DrawInvFrame(r);
        }
        CdDrawMouse(mouse_x, mouse_y);
    }

    /* Cursor changing. We don't perform this if dragging/resizing in order to
     * lock the cursor as it currently is (otherwise it'd reset to normal). */
    if (moved && !dragging_win && !resizing_win) {
        WmLock();
        struct window* deepest_child = WmGetDeepestAtPoint(mouse_x, mouse_y, false);
        if (deepest_child != NULL && (
            WmGetForegroundWindow() == deepest_child || WmIsAncestor(WmGetForegroundWindow(), deepest_child)
        )) {
            struct point pt;
            pt.x = mouse_x - deepest_child->global_offset_cached.x;
            pt.y = mouse_y - deepest_child->global_offset_cached.y;
            WmPostMessage((struct msg) {
                .win = deepest_child,
                .type = WM_SETCURSOR,
                .point_arg1 = pt
            });
        } else {
            /* Not over any normal window - i.e. it's over the desktop. */
            WmUnlock();
            struct point pt;
            pt.x = mouse_x;
            pt.y = mouse_y;
            WmPostMessage((struct msg) {
                .win = WmGetDesktop(),
                .type = WM_SETCURSOR,
                .point_arg1 = pt
            });
        }
        WmUnlock();
    }

    prev_mouse_buttons = mouse_buttons;
    mouse_buttons = click_bits;

    // Mouse down
    if ((mouse_buttons & MOUSE_BUTTON_LEFT) && !(prev_mouse_buttons & MOUSE_BUTTON_LEFT)) {
        struct window* win = WmGetToplevelAtPoint(mouse_x, mouse_y, true);
        
        if (win != NULL) {
            WmCallWinProc(win, (struct msg) {
                .win = win,
                .type = WM_TOPLEVEL_MOUSEDOWN,
                .point_arg1 = (struct point) {
                    .x = mouse_x,
                    .y = mouse_y,
                }
            });
            DerefObject(win);
        } else {
            WmSetForegroundWindow(NULL, true);
        }
    }
    
    bool retv = false;

    // Mouse up
    if (!(mouse_buttons & MOUSE_BUTTON_LEFT) && (prev_mouse_buttons & MOUSE_BUTTON_LEFT)) {
        if (dragging_win != NULL) {
            WmStopDraggingWindow();
            retv = true;
        }
        if (resizing_win != NULL) {
            WmStopResizingWindow();
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
        DiscardPage();
        PurgeScaledGlyphCacheIfNeeded();
        PurgeOldVnodes();
        KePurgeFreeAreasFromHeap();
        break;

    case SYSMSG_MOUSEEVENT:
        HandleMouse(msg.rect_arg.x, msg.rect_arg.y, msg.i_arg);
        break;
    }
}

static void RepaintIfNeeded(struct window* win) {
    if (WmCheckIfPaintRequired(win)) {
        WmCallWinProc(win, (struct msg) {
            .type = WM_PAINT,
            .win = win
        });
    }
}

/*
static void TestDiskRead(void*) {
    BeginNewThread();
    struct file* f;
    OpenFile("raw-fd0:", O_RDONLY, 0, &f);
    uint8_t* fmm = AllocFileMemory(f, 0, 1440 * 1024, 0, 0);
    for (int i = 0; i < 1440 * 2; ++i) {
        uint8_t* fm = fmm + i * 512;
        LogPrintf("Sector %d: %X %X %X ... %X %X\n", i, fm[0], fm[1], fm[2], fm[510], fm[511]);
    }
    FreeMemory(fmm, 1440 * 1024);
    LogPrintf("Freed memory.\n");
    DerefObject(f);

    int res = OpenFile("drv0:/test3.c", O_RDONLY, 0, &f);
    LogPrintf("OpenFile: res = %d\n", res);
    fmm = AllocFileMemory(f, 0, 4096, 0, 0);
    for (int i = 0; i < 4096; ++i) {
        LogPrintf("%c (0x%X)\n", fmm[i], fmm[i]);
    }
    FreeMemory(fmm, 4096);
    DerefObject(f);

    while (true) {
        SleepMilli(100000);
        Schedule();
    }
}*/

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

    win = WmCreateWindow(NULL, WmGetDesktop(), NULL, w1pos, true);
    win2 = WmCreateWindow(NULL, WmGetDesktop(), NULL, w2pos, true);
    win3 = WmCreateWindow(NULL, win2, NULL, w3pos, true);
    
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

    //CreateThread(GetKernelVas(), TestDiskRead, NULL);

    size_t prev_kb_free = 0;
    while (true) {
        extern bool anything_happened;
        LogPrintf("Getting sysmsg...\n");
        KeGetMessage(sys_mbox, &msg, TIMEOUT_INFINITE);
        ProcessMessage(msg);

        LogPrintf("Getting desktop...\n");
        int r = WmAppGetMessage(WmGetDesktop()->app, &msg, true);
        if (r == 0) {
            LogPrintf("Dispatching desktop...\n");
            WmDispatchMessage(msg);
        }

        if (anything_happened) {
            anything_happened = false;      
            RepaintIfNeeded(WmGetDesktop());
            RepaintIfNeeded(win);
            RepaintIfNeeded(win2);
            RepaintIfNeeded(win3);
        }

        {
            size_t kb_free = GetFreeMemory() / 1024;
            char str[16];
            strcpy(str, "---- KB free");
            str[3] = ((kb_free / 1) % 10) + '0';
            str[2] = ((kb_free / 10) % 10) + '0';
            str[1] = ((kb_free / 100) % 10) + '0';
            str[0] = ((kb_free / 1000) % 10) + '0';

            if (prev_kb_free != kb_free) {
                prev_kb_free = kb_free;
                struct region rgn = CdCreateRectRegion(0, 0, 200, 20);
                WmInvalidateRegion(WmGetDesktop(), rgn, true);
                CdFreeRegion(rgn);
                RepaintIfNeeded(WmGetDesktop());    
            }
            extern void VGADrawString(int x, int y, int pt, uint8_t col, const char* s, bool bold, bool italic);
            VGADrawString(0, 0, 12, 0, str, false, false);
        }
    }

    (void) ProcessMessage;
    (void) msg;
}
