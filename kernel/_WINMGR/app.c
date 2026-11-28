#include <msgbox.h>
#include <common.h>
#include <timer.h>
#include <heap.h>
#include <string.h>
#include <log.h>
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
    /* There is no foreground window when the user has clicked on the desktop. */
    struct window* fg = WmGetForegroundWindow();
    bool is_toplevel_thread = fg != NULL && fg->app == app;
    return is_toplevel_thread && app->caret_level >= 0;
}


static void WmAppInvertCaret(struct app* app, uint64_t now) {
    // TODO: actually draw the caret!
    // invert on app->caret_owner 
    app->caret_inverted = !app->caret_inverted;
    app->timeof_last_caret_invert = now;
}

export void WmAppRestartCaretBlink(struct app* app, uint64_t blink_duration) {
    uint64_t now = KeGetTimeSinceBoot();
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

export int WmPostOrDispatchMessage(struct msg msg, bool local) {
    if (local) {
        return WmDispatchMessage(msg);
    } else {
        return WmPostMessage(msg);
    }
}

static void WmCheckGenerateSynthMessages(struct app* app) {
    /* Win manager must be locked before calling! */

    if (app == NULL) {
        return;
    }

    /* Anything that is dirty now gets dealt with by this pass, so any wake-up for it is no longer needed. Cleared before
       painting (which unlocks), so that whatever is invalidated from now on sends a new one. */
    app->synth_wake_pending = false;

    struct window* win = app->win_list_head;
    while (win) {
        if (WmCheckIfPaintRequired(win)) {
            KeLogPrintf("Dispatching WM_PAINT (WmCheckGenerateSynthMessages)...\n");
            WmUnlock();
            WmDispatchMessage((struct msg) {
                .win = win,
                .type = WM_PAINT,
            });
            WmLock();
            win = app->win_list_head;       /* Restart whole loop as we unlocked*/
            continue;
        }
        win = win->app_next_win;
    }
}

/* An app is hung if it has been running for over 5 seconds without asking for another message. Windows without an app
 * (the desktop's is fine, but nothing else is guaranteed) can't be hung. Pass lock = false if you already hold WmLock. */
/*
 * Wakes up the application that owns `win` so that it paints it. Applications that are blocked in WmAppGetMessage only
 * look for things to paint when something is posted to them, which an invalidation alone doesn't do.
 *
 * Only one wake-up is outstanding at a time, so a burst of invalidations doesn't fill the message box. If the box is
 * full anyway, that's fine: the application has messages to get through, and looks for things to paint whenever it
 * runs out of them. The window manager lock must be held.
 */
void WmNotifyDirty(struct window* win) {
    struct app* app = win->app;
    if (app == NULL || app->synth_wake_pending) {
        return;
    }

    int res;
    struct window* desktop = WmGetDesktop();
    if (desktop == NULL) {
        /* Still creating the desktop. The window manager paints it by itself when it starts. */
        return;
    }
    if (desktop->app == app) {
        /* The window manager's own thread is what runs the desktop, and it waits on the system message box. */
        struct msg wake = {.type = SYSMSG_WM_WAKE};
        res = KePostMessage(KeGetSystemMessageBox(), &wake, TIMEOUT_INSTANT);
    } else {
        struct msg wake = {.type = WM_CHECK_SYNTH, .win = win};
        res = KePostMessage(app->mbox, &wake, TIMEOUT_INSTANT);
    }
    if (res == 0) {
        app->synth_wake_pending = true;
    }
}

bool WmIsAppHung(struct app* app, bool lock) {
    if (app == NULL) {
        return false;
    }
    if (lock) WmLock();
    uint64_t wintime = app->hang_check_time;
    if (lock) WmUnlock();
    if (wintime == 0) {
        /* Blocking on WmAppGetMessage */
        return false;
    }
    uint64_t currenttime = KeGetTimeSinceBoot();
    return currenttime > wintime + MS_TO_NS(5000);
}

/*
 * Takes the next message without waiting, other than the wake-ups from WmNotifyDirty: those are answered here, by
 * looking for things to paint, and then the box is looked at again. The window manager lock must be held (and is
 * still held afterwards, though it is let go of while painting).
 */
static int WmAppTryGetMessage(struct app* app, struct msg* msg_out) {
    for (;;) {
        int res = KeGetMessage(app->mbox, msg_out, TIMEOUT_INSTANT);
        if (res != 0 || msg_out->type != WM_CHECK_SYNTH) {
            return res;
        }
        WmCheckGenerateSynthMessages(app);
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
    /* Not zero: that means 'blocked, waiting for a message'. Painting happens in here (in WmCheckGenerateSynthMessages),
       and an application stuck in its WM_PAINT is just as hung as one stuck anywhere else. */
    app->hang_check_time = KeGetTimeSinceBoot();
    int res = WmAppTryGetMessage(app, msg_out);
    if (res == EAGAIN) {
        WmCheckGenerateSynthMessages(app);
        res = WmAppTryGetMessage(app, msg_out);
    }
    app->hang_check_time = KeGetTimeSinceBoot();
    WmUnlock();

    if (res != EAGAIN) {
        /* A real message, or a real error. */
        return res;
    }

    for (;;) {
        uint64_t now = KeGetTimeSinceBoot();
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

        if (peek) {
            /* Nothing there, and a peek never blocks. */
            WmUnlock();
            return EAGAIN;
        }

        app->hang_check_time = 0;
        WmUnlock();
        res = KeGetMessage(app->mbox, msg_out, timeout);
        WmLock();
        app->hang_check_time = KeGetTimeSinceBoot();
        bool woken_to_paint = res == 0 && msg_out->type == WM_CHECK_SYNTH;
        if (woken_to_paint) {
            WmCheckGenerateSynthMessages(app);
        }
        WmUnlock();
        if (woken_to_paint) {
            /* Not a message for the application; go round again and keep waiting for one. */
            continue;
        }
        if (res != ETIMEDOUT) {
            return res;
        }

        /* Woke only for the caret; go round again and wait for a message. */
    }
}

struct app* WmCreateApp(void) {
    struct app* app = AllocHeap(sizeof(struct app));
    if (app == NULL) {
        return NULL;
    }
    KeInitObject(app, OBJ_APP);
    app->caret_inverted = false;
    app->caret_level = 0;
    app->caret_owner = NULL;
    app->caret_timeout_time = 0;
    app->timeof_last_caret_invert = 0;
    app->win_list_head = NULL;
    app->hang_check_time = 0;       /* not in WmAppGetMessage yet, but not hung either */
    app->synth_wake_pending = false;
    app->mbox = KeCreateMessageBox(sizeof(struct msg), APP_QUEUE_SIZE);
    if (app->mbox == NULL) {
        FreeHeap(app);
        return NULL;
    }
    return app;
}

static void WmCleanupApp(void* _app) {
    struct app* app = _app;
    if (app == NULL) {
        return;
    }
    KeDerefObject(app->mbox);
    FreeHeap(app);
}

void WmInitAppSubsystem(void) {
    KeRegisterObjectType(OBJ_APP, WmCleanupApp);
}
