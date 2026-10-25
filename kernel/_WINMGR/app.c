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

export int WmPostMessage(struct msg msg) {
    struct window* win = msg.win;
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

static void SetModifierIf(struct app* app, int keycode, int compare, bool keydown, int bit) {
    if (keycode == compare) {
        if (keydown) app->modifier_state |= bit;
        else app->modifier_state &= ~bit;
    }
}

static void UpdateVirtualKeyState(struct app* app, struct msg* msg) {
    int keycode = msg->i_arg;
    if (keycode < 0 || keycode >= (int) sizeof(app->keystate) * 8) {
        return;
    }
    bool keydown = msg->type == WM_KEYDOWN;
    if (keydown) {
        app->keystate[keycode >> 3] |= 1 << (keycode & 7);
    } else {
        app->keystate[keycode >> 3] &= ~(1 << (keycode & 7));
    }

    SetModifierIf(app, keycode, KEYCODE_LSHIFT, keydown, MODIFIER_LSHIFT);
    SetModifierIf(app, keycode, KEYCODE_RSHIFT, keydown, MODIFIER_RSHIFT);
    SetModifierIf(app, keycode, KEYCODE_LCTRL, keydown, MODIFIER_LCTRL);
    SetModifierIf(app, keycode, KEYCODE_RCTRL, keydown, MODIFIER_RCTRL);
    SetModifierIf(app, keycode, KEYCODE_LALT, keydown, MODIFIER_LALT);
    SetModifierIf(app, keycode, KEYCODE_RALT, keydown, MODIFIER_RALT);
}

/* Every message handed back to the caller goes through here. */
static int WmAppFinishGet(struct app* app, struct msg* msg_out, int res) {
    if (res == 0 && (msg_out->type == WM_KEYDOWN || msg_out->type == WM_KEYUP)) {
        UpdateVirtualKeyState(app, msg_out);
    }
    return res;
}

static void WmCheckGenerateSynthMessages(struct app* app) {
    /* Win manager must be locked before calling! */
    
    if (app == NULL) {
        return;
    }
    struct window* win = app->win_list_head;
    while (win) {
        if (WmCheckIfPaintRequired(win)) {
            WmDispatchMessage((struct msg) {
                .win = win,
                .type = WM_PAINT,
            });
        }
        win = win->app_next_win;
    }
}

export int WmAppGetMessage(struct app* app, struct msg* msg_out, bool peek) {
    /*
     * First attempt is always non-blocking. If the queue is empty, give the
     * window manager a chance to synthesise messages (WM_PAINT etc.) and look
     * again, so anything it generated is returned straight away. WmLock is
     * held across the peek and the synth call.
     */
    WmLock();
    int res = KeGetMessage(app->mbox, msg_out, TIMEOUT_INSTANT);
    if (res == EAGAIN) {
        WmCheckGenerateSynthMessages(app);
        res = KeGetMessage(app->mbox, msg_out, TIMEOUT_INSTANT);
    }
    WmUnlock();

    if (res != EAGAIN) {
        /* A real message, or a real error. */
        return WmAppFinishGet(app, msg_out, res);
    }

    for (;;) {
        uint64_t now = GetTimeSinceBoot();
        uint64_t timeout = TIMEOUT_INFINITE;

        WmLock();
        bool eligible = WmAppCaretEligible(app);
        bool blinking = eligible && now < app->caret_timeout_time;

        if (!blinking) {
            if (eligible && app->caret_inverted) {
                /* Hide it once it reaches timeout. */
                WmAppInvertCaret(app, now);
            }
        } else {
            uint64_t next_blink = app->timeof_last_caret_invert + WmGetCaretBlinkRate();
            if (now >= next_blink) {
                WmAppInvertCaret(app, now);
                next_blink = now + WmGetCaretBlinkRate();
            }

            /* Wake for whichever comes first: the next blink, or the end of blinking. */
            uint64_t deadline = next_blink < app->caret_timeout_time
                              ? next_blink : app->caret_timeout_time;
            timeout = deadline - now;
        }
        WmUnlock();

        if (peek) {
            /* Nothing there, and a peek never blocks. */
            return EAGAIN;
        }

        res = KeGetMessage(app->mbox, msg_out, timeout);
        if (res != ETIMEDOUT) {
            return WmAppFinishGet(app, msg_out, res);
        }

        /* Woke only for the caret; go round again and wait for a message. */
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
    app->win_list_head = NULL;
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