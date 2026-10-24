#include <msgbox.h>
#include <common.h>
#include <timer.h>
#include <heap.h>
#include <string.h>
#include <errno.h>
#include "winmgr_internal.h"
#include "../CLIPDRAW/api.h"

#define APP_QUEUE_SIZE  12

static uint64_t WmGetCaretBlinkRate(void) {
    return MS_TO_NS(500);
}

static bool WmAppCaretEligible(struct app* app) {
    if (app->caret_owner == NULL) {
        return false;
    }
    bool is_toplevel_thread = WmGetForegroundWindow()->app == app;
    return is_toplevel_thread && app->caret_level >= 0;
}

static void WmAppInvertCaret(struct app* app, uint64_t now) {
    // TODO: actually draw the caret!
    // invert on app->caret_owner 
    app->caret_inverted = !app->caret_inverted;
    app->timeof_last_caret_invert = now;
}

export void WmAppRestartCaretBlink(struct app* app, uint64_t blink_duration) {
    uint64_t now = GetTimeSinceBoot();
    if (WmAppCaretEligible(app) && app->caret_inverted) {
        WmAppInvertCaret(app, now);
    }
    app->timeof_last_caret_invert = now;
    app->caret_timeout_time = now + blink_duration;
}

export int WmPostMessage(struct window* win, struct msg msg) {
    if (win->app == NULL) {
        return EINVAL;
    }
    return KePostMessage(win->app->mbox, &msg, TIMEOUT_INSTANT);
}

export int WmDispatchMessage(struct msg msg) {
    WmLock();
    struct window* win = msg.win;
    winproc_t proc = win->winclass->proc;
    WmUnlock();
    return proc(win, msg);
}

static void UpdateVirtualKeyState(struct app* app, struct msg* msg) {
    int keycode = msg->i_arg;
    if (keycode < 0 || keycode >= (int) sizeof(app->keystate) * 8) {
        return;
    }
    if (msg->type == WM_KEYDOWN) {
        app->keystate[keycode >> 3] |= 1 << (keycode & 7);
    } else {
        app->keystate[keycode >> 3] &= ~(1 << (keycode & 7));
    }

    // TODO: modifier keys
}

export int WmAppGetMessage(struct app* app, struct msg* msg_out, bool peek) {
    while (true) {
        uint64_t now = GetTimeSinceBoot();
        WmLock();
        bool eligible = WmAppCaretEligible(app);
        bool blinking = eligible && now < app->caret_timeout_time;

        if (!blinking) {
            if (eligible && app->caret_inverted) {
                /* Hide it once it reaches timeout. */
                WmAppInvertCaret(app, now);
            }
            WmUnlock();
            return KeGetMessage(app->mbox, msg_out, peek ? TIMEOUT_INSTANT : TIMEOUT_INFINITE);

        } else {
            WmUnlock();
        }

        uint64_t next_blink = app->timeof_last_caret_invert + WmGetCaretBlinkRate();
        if (now >= next_blink) {
            WmAppInvertCaret(app, now);
            next_blink = now + WmGetCaretBlinkRate();
        }

        /* Wake for whichever comes first: the next blink, or the end of blinking. */
        uint64_t deadline = next_blink < app->caret_timeout_time
                          ? next_blink : app->caret_timeout_time;

        int res = KeGetMessage(app->mbox, msg_out, peek ? TIMEOUT_INSTANT : deadline - now);
        if ((!peek && res != ETIMEDOUT) || (peek && res != EAGAIN)) {
            /* We got a real message or an actual error, return either of those
             * to the user. 
             */
            if (msg_out->type == WM_KEYDOWN || msg_out->type == WM_KEYUP) {
                UpdateVirtualKeyState(app, msg_out);
            }
            return res;
        }

        /* 
         * No message received yet, but we woke to handle the caret, so handle
         * that and wait again for the message.
         */
    }
}

export bool WmIsKeyDown(struct app* app, int keycode) {
    if (keycode < 0 || keycode >= (int) sizeof(app->keystate) * 8) {
        return false;
    }
    return (app->keystate[keycode >> 3] >> (keycode & 7)) & 1;
}

export bool WmIsShiftHeld(struct app* app) {
    return !!(app->modifier_state & (MODIFIER_LSHIFT | MODIFIER_RSHIFT));
}

export bool WmIsAltHeld(struct app* app) {
    return !!(app->modifier_state & (MODIFIER_LALT | MODIFIER_RALT));
}

export bool WmIsControlHeld(struct app* app) {
    return !!(app->modifier_state & (MODIFIER_LCTRL | MODIFIER_RCTRL));
}

struct app* WmCreateApp(void) {
    struct app* app = AllocHeap(sizeof(struct app));
    InitUserObject(app, UOBJ_APP);
    app->caret_inverted = false;
    app->caret_level = 0;
    app->caret_owner = NULL;
    app->caret_timeout_time = 0;
    app->timeof_last_caret_invert = 0;
    app->modifier_state = 0;
    app->mbox = CreateMessageBox(sizeof(struct msg), APP_QUEUE_SIZE);
    memset(app->keystate, 0, sizeof(app->keystate));
    return app;
}

static void CleanupApp(void* _app) {
    struct app* app = _app;
    DerefObject(app->mbox);
    FreeHeap(app);
}

void WmInitAppSubsystem(void) {
    RegisterUserObjectType(UOBJ_APP, CleanupApp);
}