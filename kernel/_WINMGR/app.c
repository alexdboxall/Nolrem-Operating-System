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

bool WmIsAppHung(struct app* app) {
    WmLock();
    uint64_t wintime = app->hang_check_time;
    WmUnlock();
    if (wintime == 0) {
        /* Blocking on WmAppGetMessage */
        return false;
    }
    uint64_t currenttime = KeGetTimeSinceBoot();
    return currenttime > wintime + MS_TO_NS(5000);
}

export int WmAppGetMessage(struct app* app, struct msg* msg_out, bool peek) {
    /*
     * First attempt is always non-blocking. If the queue is empty, give the
     * window manager a chance to synthesise messages (WM_PAINT etc.) and look
     * again, so anything it generated is returned straight away. WmLock is
     * held across the peek and the synth call.
     */
    WmLock();
    app->hang_check_time = 0;
    int res = KeGetMessage(app->mbox, msg_out, TIMEOUT_INSTANT);
    if (res == EAGAIN) {
        WmCheckGenerateSynthMessages(app);
        res = KeGetMessage(app->mbox, msg_out, TIMEOUT_INSTANT);
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
        WmUnlock();
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
