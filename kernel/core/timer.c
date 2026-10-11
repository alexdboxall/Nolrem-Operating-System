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

uint64_t GetTimeSinceBoot(void) {
    AcquireSpinlock(&timer_spinlock);
    uint64_t retv = ns_since_boot;
    ReleaseSpinlock(&timer_spinlock);
    return retv;
}

static void DoWakeups(uint64_t ns) {
    while (first_waiting_timer != NULL) {
        struct thread* thr = first_waiting_timer;
        if (ns >= thr->wakeup_time_ns) {
            thr->wakeup_time_ns = 0;
            first_waiting_timer = thr->next_waiting_timer;
            UnblockThread(thr, ETIMEDOUT);
        } else {
            next_wakeup_ns = thr->wakeup_time_ns;
            return;
        }     
    }
    next_wakeup_ns = 0;
}

void AdvanceTimer(uint64_t ns) {
    AcquireScheduler();
    AcquireSpinlock(&timer_spinlock);
    ns_since_boot += ns;
    if (next_wakeup_ns != 0 && next_wakeup_ns <= ns_since_boot) {
        DoWakeups(ns_since_boot);
    }
    ReleaseSpinlock(&timer_spinlock);
    ReleaseScheduler();
}

void CancelTimer(struct thread* thr) {
    // Scheduler lock must already be held!

    if (thr->wakeup_time_ns == 0) {
        /* Already cancelled, i.e. DoWakeups woke it, so CancelTimer as part
         * of UnblockThread doesn't need to do anything. */
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

    AcquireSpinlock(&timer_spinlock); 
    next_wakeup_ns = first_waiting_timer == NULL ? 0 : first_waiting_timer->wakeup_time_ns;
    ReleaseSpinlock(&timer_spinlock);
}

bool AddThreadToSleepQueue(bool relative, uint64_t ns, bool need_sched_lock) {
    if (need_sched_lock) AcquireScheduler();

    AcquireSpinlock(&timer_spinlock); 
    /* Convert to absolute. */
    if (relative) {
        ns += ns_since_boot;
    }
    bool past = ns <= ns_since_boot;
    if (past) {
        ReleaseSpinlock(&timer_spinlock);
        if (need_sched_lock) ReleaseScheduler();
        return false;
    }
    if (next_wakeup_ns == 0 || ns < next_wakeup_ns) {
        next_wakeup_ns = ns;
    }
    ReleaseSpinlock(&timer_spinlock);

    struct thread* thr = GetCurrentThread();
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

    BlockThread();
    if (need_sched_lock) ReleaseScheduler();
    return true;
}

void InitTimer(void) {
    InitSpinlock(&timer_spinlock);
}

export void SleepUntil(uint64_t ns) {
    AddThreadToSleepQueue(false, ns, true);
}

export void SleepNano(uint64_t ns) {
    AddThreadToSleepQueue(true, ns, true);
}

export void SleepMilli(uint64_t ms) {
    AddThreadToSleepQueue(true, ms * 1000000, true);
}
