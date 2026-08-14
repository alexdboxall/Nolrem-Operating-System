#include <common.h>
#include <thread.h>
#include <obj.h>
#include <scheduler.h>

static struct thread* current_thread;
static struct thread dummy;

export struct thread* GetCurrentThread(void) {
    return current_thread;
}

void SetThreadWaitingSem(struct thread* thr, struct sem* sem) {
    if (sem == NULL) {
        if (thr->waiting_sem != NULL) {
            DerefObject(thr->waiting_sem);
        }
    } else {
        RefObject(sem);
    }
    thr->waiting_sem = sem;
}

void BlockThread(void) {
    // Scheduler lock must already be held!
    GetCurrentThread()->state = THREAD_STATE_BLOCKED;
    GetCurrentThread()->block_return_val = 0;
    Schedule();
}

void UnblockThread(struct thread* thr, int retv) {
    // Scheduler lock must already be held!
    // TODO: add back to the list
    // TODO: sort out semaphore cancellation, etc.
    thr->block_return_val = retv;
}

void InitThread(void) { 
    current_thread = &dummy;
}