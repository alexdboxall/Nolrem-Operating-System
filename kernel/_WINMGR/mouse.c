
#include <obj.h>
#include <heap.h>
#include <log.h>
#include <mutex.h>
#include "winmgr_internal.h"

static struct rect mouse_bounds = {.x = 0, .y = 0, .w = 640, .h = 480};
static int mouse_x = 320 - 4;
static int mouse_y = 240 - 8;

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

void WmHandleMouseInput(uint8_t click_bits, int16_t delta_x, int16_t delta_y, int scrollx, int scrolly) {
    int new_mx = mouse_x + delta_x;
    int new_my = mouse_y + delta_y;
    BoundMouse(&new_mx, &new_my, &mouse_bounds);

    if (new_mx != mouse_x || new_my != mouse_y) {
        CdRemoveMouse(mouse_x, mouse_y);
        mouse_x = new_mx;
        mouse_y = new_my;
        CdDrawMouse(mouse_x, mouse_y);
    }

    (void) click_bits;
    (void) scrollx;
    (void) scrolly;
}
