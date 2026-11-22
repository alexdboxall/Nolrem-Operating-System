#include <timer.h>
#include <common.h>
#include <spinlock.h>
#include <scheduler.h>
#include <thread.h>
#include <errno.h>

struct spinlock timer_spinlock;
static uint64_t ns_since_boot = 1;
static uint64_t next_wakeup_ns = 0;
static struct thread* first_waiting_timer = NULL;

export uint64_t KeGetTimeSinceBoot(void) {
    KeAcquireSpinlock(&timer_spinlock);
    uint64_t retv = ns_since_boot;
    KeReleaseSpinlock(&timer_spinlock);
    return retv;
}

static void KeDoWakeups(uint64_t ns) {
    while (first_waiting_timer != NULL) {
        struct thread* thr = first_waiting_timer;
        if (ns >= thr->wakeup_time_ns) {
            thr->wakeup_time_ns = 0;
            first_waiting_timer = thr->next_waiting_timer;
            KeUnblockThread(thr, ETIMEDOUT);
        } else {
            next_wakeup_ns = thr->wakeup_time_ns;
            return;
        }     
    }
    next_wakeup_ns = 0;
}

void KeAdvanceTimer(uint64_t ns) {
    KeAcquireScheduler();
    KeAcquireSpinlock(&timer_spinlock);
    ns_since_boot += ns;
    if (next_wakeup_ns != 0 && next_wakeup_ns <= ns_since_boot) {
        KeDoWakeups(ns_since_boot);
    }
    KeReleaseSpinlock(&timer_spinlock);
    KeReleaseScheduler();
}

void KeCancelTimer(struct thread* thr) {
    // Scheduler lock must already be held!

    if (thr->wakeup_time_ns == 0) {
        /* Already cancelled, i.e. KeDoWakeups woke it, so KeCancelTimer as part
         * of KeUnblockThread doesn't need to do anything. */
        return;
    }
    
    if (first_waiting_timer == thr) {
        first_waiting_timer = thr->next_waiting_timer;
    } else {
        struct thread* prev = first_waiting_timer;
        while (prev != NULL && prev->next_waiting_timer != thr) {
            prev = prev->next_waiting_timer;
        }
        if (prev != NULL) prev->next_waiting_timer = thr->next_waiting_timer;
    }

    thr->next_waiting_timer = NULL;
    thr->wakeup_time_ns = 0;

    KeAcquireSpinlock(&timer_spinlock); 
    next_wakeup_ns = first_waiting_timer == NULL ? 0 : first_waiting_timer->wakeup_time_ns;
    KeReleaseSpinlock(&timer_spinlock);
}

bool KeAddThreadToSleepQueue(bool relative, uint64_t ns, bool need_sched_lock) {
    if (need_sched_lock) KeAcquireScheduler();

    KeAcquireSpinlock(&timer_spinlock); 
    /* Convert to absolute. */
    if (relative) {
        ns += ns_since_boot;
    }
    bool past = ns <= ns_since_boot;
    if (past) {
        KeReleaseSpinlock(&timer_spinlock);
        if (need_sched_lock) KeReleaseScheduler();
        return false;
    }
    if (next_wakeup_ns == 0 || ns < next_wakeup_ns) {
        next_wakeup_ns = ns;
    }
    KeReleaseSpinlock(&timer_spinlock);

    struct thread* thr = KeGetCurrentThread();
    thr->wakeup_time_ns = ns;
    if (first_waiting_timer == NULL || ns < first_waiting_timer->wakeup_time_ns) {
        thr->next_waiting_timer = first_waiting_timer;
        first_waiting_timer = thr;
    } else {
        struct thread* prev = first_waiting_timer;
        while (prev->next_waiting_timer != NULL && prev->next_waiting_timer->wakeup_time_ns <= ns) {
            prev = prev->next_waiting_timer;
        }  
        thr->next_waiting_timer = prev->next_waiting_timer;
        prev->next_waiting_timer = thr;
    }

    KeBlockThread();
    if (need_sched_lock) KeReleaseScheduler();
    return true;
}

void KeInitTimer(void) {
    KeInitSpinlock(&timer_spinlock);
}

export void KeSleepUntil(uint64_t ns) {
    KeAddThreadToSleepQueue(false, ns, true);
}

export void KeSleepNano(uint64_t ns) {
    KeAddThreadToSleepQueue(true, ns, true);
}

export void KeSleepMilli(uint64_t ms) {
    KeAddThreadToSleepQueue(true, ms * 1000000, true);
}
