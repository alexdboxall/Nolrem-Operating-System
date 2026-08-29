#pragma once

#include "../CLIPDRAW/api.h"
#include "../CLIPDRAW/clipdraw_internal.h"

struct window_class {
    struct user_obj_header hdr;

    char* name;
    winproc_t proc;
    struct window_class* next;
    int flags;
};

struct window {
    struct user_obj_header hdr;

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
};

struct dc* WmGetDC(void);
int WmReturnDC(struct dc* dc);
struct dc* WmBeginPaint(struct window* win);
void WmEndPaint(struct window* win, struct dc* dc);
void WmChangePosition(struct window* win, struct rect local_r, bool lock);
struct window* WmCreateWindow(struct window* parent, const char* classname, struct rect local_r, bool lock);
struct window_class* WmOpenWindowClass(const char* name);
struct window_class* WmCreateWindowClass(const char* name, winproc_t proc, int flags);
int WmDefaultWindowProcedure(struct window* win, struct msg msg);
struct window* WmGetDesktop(void);
int WmCallWinProc(struct window* win, struct msg msg);
void WmInvalidateRegion(struct window* win, struct region rgn, bool lock);
void WmInvalidateWindow(struct window* win, bool lock);