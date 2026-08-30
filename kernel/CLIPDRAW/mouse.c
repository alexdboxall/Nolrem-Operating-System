#include "clipdraw_internal.h"
#include "../_WINMGR/winmgr_internal.h"
#include "api.h"
#include <obj.h>
#include <errno.h>
#include <panic.h>
#include <kgfx.h>
#include <heap.h>
#include <spinlock.h>
#include <string.h>

#define MOUSE_WIDTH  12
#define MOUSE_HEIGHT 19

static struct spinlock video_lock;

extern void VgaInvertRect(struct graphics_driver* drv, int x1, int y1, int x2, int y2);
extern colour_t VGAReadPixel(struct graphics_driver*, int x, int y);
extern void VgaPutSolidRect(struct graphics_driver*, int x1, int y1, int x2, int y2, uint32_t colour);

static const uint32_t mouse_cursor_black[MOUSE_HEIGHT] = {
    0b10000000000000000000000000000000, // "1..........."
    0b11000000000000000000000000000000, // "11.........."
    0b10100000000000000000000000000000, // "121........."
    0b10010000000000000000000000000000, // "1221........"
    0b10001000000000000000000000000000, // "12221......."
    0b10000100000000000000000000000000, // "122221......"
    0b10000010000000000000000000000000, // "1222221....."
    0b10000001000000000000000000000000, // "12222221...."
    0b10000000100000000000000000000000, // "122222221..."
    0b10000000010000000000000000000000, // "1222222221.."
    0b10000011111000000000000000000000, // "12222211111."
    0b10010010000000000000000000000000, // "1221221....."
    0b10101001000000000000000000000000, // "121.1221...."
    0b11001001000000000000000000000000, // "11..1221...."
    0b10000100100000000000000000000000, // "1....1221..."
    0b00000100100000000000000000000000, // ".....1221..."
    0b00000010010000000000000000000000, // "......1221.."
    0b00000010010000000000000000000000, // "......1221.."
    0b00000001100000000000000000000000, // ".......11..."
};

static const uint32_t mouse_cursor_white[MOUSE_HEIGHT] = {
    0b00000000000000000000000000000000, // "1..........."
    0b00000000000000000000000000000000, // "11.........."
    0b01000000000000000000000000000000, // "121........."
    0b01100000000000000000000000000000, // "1221........"
    0b01110000000000000000000000000000, // "12221......."
    0b01111000000000000000000000000000, // "122221......"
    0b01111100000000000000000000000000, // "1222221....."
    0b01111110000000000000000000000000, // "12222221...."
    0b01111111000000000000000000000000, // "122222221..."
    0b01111111100000000000000000000000, // "1222222221.."
    0b01111100000000000000000000000000, // "12222211111."
    0b01101100000000000000000000000000, // "1221221....."
    0b01000110000000000000000000000000, // "121.1221...."
    0b00000110000000000000000000000000, // "11..1221...."
    0b00000011000000000000000000000000, // "1....1221..."
    0b00000011000000000000000000000000, // ".....1221..."
    0b00000001100000000000000000000000, // "......1221.."
    0b00000001100000000000000000000000, // "......1221.."
    0b00000000000000000000000000000000, // ".......11..."
};

static bool mouse_onscreen = false;   // is a cursor actually painted right now?
static int mouse_screen_x = 0;
static int mouse_screen_y = 0;

static struct graphics_driver* GetMouseDriver(void) {
    static struct graphics_driver* drv = NULL;
    if (drv == NULL) {
        drv = GetKernelGraphicsDriver();
        struct graphics_capabilities caps = drv->get_capabilities(drv);
        int bytes = MOUSE_HEIGHT * MOUSE_WIDTH * 4;
        if (caps.desired_mouse_restore_buffer_mode == MOUSE_BUFFER_UINT8) bytes /= 4;
        if (caps.desired_mouse_restore_buffer_mode == MOUSE_BUFFER_UINT16) bytes /= 2;
        drv->mouse_restore_buffer = AllocHeap(bytes);
    }
    return drv;
}

static void RealDrawMouse(int x, int y) {
    struct graphics_driver* drv = GetMouseDriver();
    drv->draw_mouse(
        drv, x, y, mouse_cursor_black, mouse_cursor_white, drv->mouse_restore_buffer,
        MOUSE_WIDTH, MOUSE_HEIGHT
    );
    mouse_onscreen = true;
    mouse_screen_x = x;
    mouse_screen_y = y;
}

static void RealRemoveMouse(int x, int y) {
    struct graphics_driver* drv = GetMouseDriver();
    drv->remove_mouse(drv, x, y, drv->mouse_restore_buffer, MOUSE_WIDTH, MOUSE_HEIGHT);
    mouse_onscreen = false;
}

int buffered_mouse_remove_x;
int buffered_mouse_remove_y;
int buffered_mouse_draw_x;
int buffered_mouse_draw_y;
bool need_mouse_remove = false;
bool need_mouse_draw = false;

static void DoBufferedMouse(void) {
    if (need_mouse_remove) {
        need_mouse_remove = false;
        RealRemoveMouse(buffered_mouse_remove_x, buffered_mouse_remove_y);
    }
    if (need_mouse_draw) {
        need_mouse_draw = false;
        RealDrawMouse(buffered_mouse_draw_x, buffered_mouse_draw_y);
    }
}

void CdRemoveMouse(int x, int y) {
    if (TryAcquireSpinlock(&video_lock)) {
        DoBufferedMouse();
        RealRemoveMouse(x, y);
        ReleaseSpinlock(&video_lock);
    } else {
        if (!need_mouse_remove) {
            need_mouse_remove = true;
            buffered_mouse_remove_x = x;
            buffered_mouse_remove_y = y;
        }
    }
}

void CdDrawMouse(int x, int y) {
    if (TryAcquireSpinlock(&video_lock)) {
        DoBufferedMouse();
        RealDrawMouse(x, y);
        ReleaseSpinlock(&video_lock);
    } else {
        need_mouse_draw = true;
        buffered_mouse_draw_x = x;
        buffered_mouse_draw_y = y;
    }
}

static bool hid_mouse = false;

void CdStartVideoUpdate(int x1, int y1, int x2, int y2) {
    AcquireSpinlock(&video_lock);

    if (mouse_onscreen &&
        mouse_screen_x < x2 && mouse_screen_x + MOUSE_WIDTH  > x1 &&
        mouse_screen_y < y2 && mouse_screen_y + MOUSE_HEIGHT > y1) {
        RealRemoveMouse(mouse_screen_x, mouse_screen_y);
        hid_mouse = true;
    }
}

void CdEndVideoUpdate(int x1, int y1, int x2, int y2) {
    (void) x1; (void) y1; (void) x2; (void) y2;

    if (need_mouse_remove) {
        need_mouse_remove = false;
        if (!hid_mouse) {
            RealRemoveMouse(buffered_mouse_remove_x, buffered_mouse_remove_y);
        }
    }

    if (need_mouse_draw) {
        need_mouse_draw = false;
        RealDrawMouse(buffered_mouse_draw_x, buffered_mouse_draw_y);
    } else if (hid_mouse) {
        // no move happened during the paint, so mouse_screen_x/y (still holding
        // the pre-hide position, since RealRemoveMouse doesn't touch it) is
        // exactly where it needs to reappear.
        RealDrawMouse(mouse_screen_x, mouse_screen_y);
    }

    hid_mouse = false;
    ReleaseSpinlock(&video_lock);
}

void CdInitMouseSubsystem(void) {
    InitSpinlock(&video_lock);
}