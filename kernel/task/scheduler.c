#include <common.h>
#include <scheduler.h>
#include <spinlock.h>

static struct spinlock sched_lock;
static int sched_prevent_count = 0;
static bool sched_postponed = false;

export void AcquireScheduler(void) {
    AcquireSpinlock(&sched_lock);
    sched_prevent_count++;
    ReleaseSpinlock(&sched_lock);
}

export void ReleaseScheduler(void) {
    AcquireSpinlock(&sched_lock);
    bool zero = (--sched_prevent_count) == 0;
    ReleaseSpinlock(&sched_lock);
    if (zero) {
        Schedule();
    }
}

static struct thread* FindNextThread(void) {
    return NULL;
}

void SwitchToThread(struct thread* thr) {
    if (thr == NULL) {
        thr = FindNextThread();
    }
    
    // ...
}

export void Schedule(void) {
    AcquireSpinlock(&sched_lock);
    if (sched_prevent_count != 0) {
        sched_postponed = true;
    } else {
        sched_postponed = false;
        SwitchToThread(NULL);
    }
    ReleaseSpinlock(&sched_lock);
}

void InitScheduler(void) {
    sched_prevent_count = 0;
    InitSpinlock(&sched_lock);
}