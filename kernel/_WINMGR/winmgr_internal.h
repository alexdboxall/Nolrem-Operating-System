#pragma once

#include "../CLIPDRAW/api.h"
#include "../CLIPDRAW/clipdraw_internal.h"
#include "api.h"

#define DOUBLE_CLICK_MS 400

#define BORDER_WIDTH    3
#define CORNER_WIDTH    12

#define SHADOW_CUT_IN   2
#define TITLEBAR_HEIGHT 20

/* How many windows can be between a window and the desktop. */
#define MAX_NESTED_WIN_DEPTH 6

struct app {
    struct obj_header hdr;
    struct msgbox* mbox;
    struct window* caret_owner;
    int caret_level;                        /* >= 0 means shown (ShowCaret/HideCaret style) */
    bool caret_inverted;                    /* currently in the 'off' phase of the blink */
    uint64_t timeof_last_caret_invert;
    uint64_t caret_timeout_time;            /* pushed into the future on each keypress, etc. */
    struct point caret_location;
    struct size caret_size;
    struct window* win_list_head;
    uint64_t hang_check_time;
    bool synth_wake_pending;                /* a wake-up (WM_CHECK_SYNTH) has been sent and not yet acted on */
};

/*
 * Posted to an application's message box when one of its windows needs painting, so that an application that is
 * waiting for a message wakes up to be given its WM_PAINT. WmAppGetMessage deals with it itself, and it is never
 * returned to the application or sent to a window procedure.
 */
#define WM_CHECK_SYNTH  0xFF00

/* Lives in usermode memory, so it is not an object (the kernel can't count references to something that the
   process can scribble on) and has no header. */
struct uapp {
    struct app* krnl_app_ptr;
    uint8_t keystate[256 / 8];
    uint16_t modifier_state;
};

struct window_class {
    struct obj_header hdr;
    winproc_t proc;
    struct window_class* next;
    struct window_class* parent;
    struct icon* icon;
    struct cursor* cursor;
    int flags;
    uint8_t extra_ptrs;
    char name[];
};

struct window {
    struct obj_header hdr;
    struct app* app;

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
    struct window* app_next_win;

    struct window_class* winclass;

    struct rect restore_pos;
    int style;

    void* extra_ptrs[];
};


void WmLock(void);
void WmUnlock(void);

struct app;

struct dc* WmGetDC(void);
int WmReturnDC(struct dc* dc);
struct app* WmCreateApp(void);
int WmAppGetMessage(struct app* app, struct msg* msg_out, bool peek);
struct dc* WmBeginPaint(struct window* win);
int WmPostMessage(struct msg msg);
void WmEndPaint(struct window* win, struct dc* dc);
void WmChangePosition(struct window* win, struct rect local_r, bool lock);
struct window* WmCreateWindow(struct app* app, struct window* parent, const char* classname, struct rect local_r, bool lock);
struct window_class* WmOpenWindowClass(const char* name);
struct window_class* WmCreateWindowClass(const char* name, winproc_t proc, int flags);
int WmDefaultWindowProcedure(struct window* win, struct msg msg);
struct window* WmGetDesktop(void);
int WmCallWinProc(struct window* win, struct msg msg);
void WmInvalidateRegion(struct window* win, struct region rgn, bool lock);
void WmInvalidateWindow(struct window* win, bool lock);
struct window* WmGetForegroundWindow();
/* Takes the lock itself, and tells the windows concerned (WM_ACTIVATE, WM_ACTIVATE_APP). NULL is 'none'. */
void WmSetForegroundWindow(struct window* win);

/* For callers that already hold the lock: change it now (WmChangeForegroundWindow), and tell the windows
 * concerned once the lock has been let go of (WmAnnounceActivation, or WmDiscardActivation for no-one). */
struct wm_activation {
    struct window* old_fg;      /* a reference, or NULL if there was no real foreground window before */
    struct window* new_fg;      /* a reference, or NULL if there is none now */
    int how;                    /* WA_ACTIVATE or WA_CLICKACTIVATE: how new_fg got it */
};
void WmChangeForegroundWindow(struct window* win, int how, struct wm_activation* change);
void WmAnnounceActivation(struct wm_activation* change);
void WmDiscardActivation(struct wm_activation* change);
struct window* WmGetToplevelAtPoint(int x, int y, bool lock);
/* Adds a reference (to the desktop, if nothing else is there), which the caller must give back. */
struct window* WmGetDeepestAtPoint(int x, int y, bool lock);
bool WmIsPointInRect(int x, int y, struct rect r);
struct point WmGetMousePositionGlobal(void);
struct rect WmGetGlobalPosition(struct window* win, bool lock);
void WmRaiseToTop(struct window* win, bool lock);
_Noreturn void WmMainloop(void);
int WmHardError(int hard_errno);
void WmHandleMouseInput(uint8_t click_bits, int16_t delta_x, int16_t delta_y, int scrollx, int scrolly);
void WmStartDraggingWindow(struct window* win);
void WmStartResizingWindow(struct window* win, int hit_test_results);
extern bool wm_mainloop_started;
bool WmCheckIfPaintRequired(struct window* win);
bool WmDoesWindowShowBorder(struct window* win);
bool WmIsAncestor(struct window* potential_ancestor, struct window* reference);
int WmDispatchMessage(struct msg msg);
int WmPostOrDispatchMessage(struct msg msg, bool local);
int WmChangeStyle(struct window* win, int style);
bool WmIsAppHung(struct app* app, bool lock);
/* Call (with the lock held) after anything adds to a window's dirty region, so its application wakes up to paint it. */
void WmNotifyDirty(struct window* win);

/*
 * The window manager's application for the process that is running (the one whose system call this is). It is the
 * application that windows made for the process belong to, and is the one that the process's messages go to.
 *
 * NOT IN YET. The syscalls in syscall/ are written to use it as it is described here, and the kernel won't link
 * until it exists: they take the pointer as borrowed (no reference is added, since the process keeps its
 * application for as long as it runs), and NULL as 'this process has no application, and can't be given one'.
 * If it is to give a counted reference instead, that is WmSysGetApp() in syscall/helpers.c, and nowhere else.
 */
struct app* WmGetAppObjForProcess(void);


/* Supply EITHER `win` or the pair `wc` and `style`. If win != NULL, wc and style ignored. */
int WmAdjustWindowRect(struct rect* rect, struct window* win, struct window_class* wc, int style, int mode);
