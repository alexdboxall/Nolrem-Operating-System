#pragma once

#include "../CLIPDRAW/api.h"
#include "../CLIPDRAW/clipdraw_internal.h"

#define DOUBLE_CLICK_MS 400

#define BORDER_WIDTH    3
#define CORNER_WIDTH    12

#define SHADOW_CUT_IN   2
#define TITLEBAR_HEIGHT 20

struct app {
    struct user_obj_header hdr;
    struct msgbox* mbox;
    struct window* caret_owner;
    int caret_level;                        /* >= 0 means shown (ShowCaret/HideCaret style) */
    bool caret_inverted;                    /* currently in the 'off' phase of the blink */
    uint64_t timeof_last_caret_invert;
    uint64_t caret_timeout_time;            /* pushed into the future on each keypress, etc. */
    struct point caret_location;
    struct size caret_size;
    uint8_t keystate[256 / 8];
    uint8_t modifier_state;
};

struct window_class {
    struct user_obj_header hdr;
    winproc_t proc;
    struct window_class* next;
    int flags;
    char name[];
};

#define WS_MAXIMISED        1

struct window {
    struct user_obj_header hdr;
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

    struct window_class* winclass;

    struct rect restore_pos;
    int style;
};


void WmLock(void);
void WmUnlock(void);

struct app;

struct dc* WmGetDC(void);
int WmReturnDC(struct dc* dc);
struct dc* WmBeginPaint(struct window* win);
int WmPostMessage(struct window* win, struct msg msg);
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
void WmSetForegroundWindow(struct window* win, bool lock);
struct window* WmGetToplevelAtPoint(int x, int y, bool lock);
struct window* WmGetDeepestAtPoint(int x, int y, bool lock);
bool WmIsPointInRect(int x, int y, struct rect r);
struct point WmGetMousePositionGlobal(void);
struct rect WmGetGlobalPosition(struct window* win, bool lock);
void WmRaiseToTop(struct window* win, bool lock);
_Noreturn void WmMainloop(void);
struct msgbox* WmGetSystemMessageBox(void);
void WmInitSystemMessageBox(void);
void WmStartDraggingWindow(struct window* win);
void WmStartResizingWindow(struct window* win, int hit_test_results);
extern bool wm_mainloop_started;
bool WmCheckIfPaintRequired(struct window* win);
bool DoesWindowShowBorder(struct window* win);
bool WmIsAncestor(struct window* potential_ancestor, struct window* reference);