
#include <obj.h>
#include <heap.h>
#include <log.h>
#include <mutex.h>
#include "winmgr_internal.h"

static struct rect mouse_bounds = {.x = 0, .y = 0, .w = 640, .h = 480};
static int mouse_x = 320 - 4;
static int mouse_y = 240 - 8;
static int mouse_buttons = 0;
static int prev_mouse_buttons = 0;

#define MOUSE_BUTTON_LEFT           1
 
struct graphics_driver;

static void BoundMouse(int* x, int* y, struct rect* mouse_bounds) {
    if (*x < mouse_bounds->x) *x = mouse_bounds->x;
    if (*y < mouse_bounds->y) *y = mouse_bounds->y;
    if (*x >= mouse_bounds->x + mouse_bounds->w) *x = mouse_bounds->x + mouse_bounds->w - 1;
    if (*y >= mouse_bounds->y + mouse_bounds->h) *y = mouse_bounds->y + mouse_bounds->h - 1;
}

export struct point WmGetMousePositionGlobal(void) {
    return (struct point) {.x = mouse_x, .y = mouse_y};
}

static struct window* dragging_win = NULL;
static int drag_mouse_start_x = 0;
static int drag_mouse_start_y = 0;
static struct rect drag_win_og_bounds;


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
    // the mouse IRQ ought not to be doing this - should be done on a msg
    // thread. probably need to send a message to the desktop? and it can
    // process this
    drag_mouse_start_x = mouse_x;
    drag_mouse_start_y = mouse_y;
    drag_win_og_bounds = win->local_client_bound;
    dragging_win = win;

    WmSetForegroundWindow(win, true);
    WmRaiseToTop(win, true);
}

static void WmStopDraggingWindow(void) {
    // the mouse IRQ ought not to be doing this - should be done on a msg
    // thread. probably need to send a message to the desktop? and it can
    // process this
    if (dragging_win != NULL) {
        struct rect r = dragging_win->local_win_bound;
        r.x += mouse_x - drag_mouse_start_x;
        r.y += mouse_y - drag_mouse_start_y;
        WmChangePosition(dragging_win, r, true); 
        DerefObject(dragging_win);
        dragging_win = NULL;
    }
}

void WmHandleMouseInput(uint8_t click_bits, int16_t delta_x, int16_t delta_y, int scrollx, int scrolly) {
    int new_mx = mouse_x + delta_x;
    int new_my = mouse_y + delta_y;
    bool moved = new_mx != mouse_x || new_my != mouse_y;
    BoundMouse(&new_mx, &new_my, &mouse_bounds);

    if (moved) {
        CdRemoveMouse(mouse_x, mouse_y);
        if (dragging_win) {
            struct rect r = dragging_win->local_win_bound;
            r.x += mouse_x - drag_mouse_start_x;
            r.y += mouse_y - drag_mouse_start_y;
            DrawInvFrame(r);
        }
        mouse_x = new_mx;
        mouse_y = new_my;
        if (dragging_win) {
            struct rect r = dragging_win->local_win_bound;
            r.x += mouse_x - drag_mouse_start_x;
            r.y += mouse_y - drag_mouse_start_y;
            DrawInvFrame(r);
        }
        CdDrawMouse(mouse_x, mouse_y);
    }

    prev_mouse_buttons = mouse_buttons;
    mouse_buttons = click_bits;

    /* Mouse down */
    if ((mouse_buttons & MOUSE_BUTTON_LEFT) && !(prev_mouse_buttons & MOUSE_BUTTON_LEFT)) {
        struct window* win = WmGetToplevelAtPoint(mouse_x, mouse_y, true);
        if (win != NULL) {
            WmStartDraggingWindow(win);
        }
    }
    
    /* Mouse up */
    if (!(mouse_buttons & MOUSE_BUTTON_LEFT) && (prev_mouse_buttons & MOUSE_BUTTON_LEFT)) {
        if (dragging_win != NULL) {
            WmStopDraggingWindow();
        }
    }

    (void) scrollx;
    (void) scrolly;
}
