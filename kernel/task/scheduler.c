#include <common.h>
#include <scheduler.h>
#include <thread.h>
#include <spinlock.h>
#include <vmm.h>
#include <arch.h>
#include <panic.h>
#include <log.h>
#include <arch.h>
#include <msgbox.h>

static struct spinlock sched_lock;
static int sched_prevent_count = 0;
static bool sched_postponed = false;
static struct thread* sched_postponed_thr = NULL;
static struct thread* ready_list_head = NULL;
static struct thread* ready_list_tail = NULL;
static struct thread* current_thread = NULL;
static bool sched_init = false;
static struct msg defer_msg;
static bool has_defer_msg = 0;

export bool IsSchedulingInitialised(void) {
    return sched_init;
}

export struct thread* GetCurrentThread(void) {
    return current_thread;
}

export void AcquireScheduler(void) {
    ArchDisableInterrupts();
    AcquireSpinlock(&sched_lock);
    sched_prevent_count++;
    ReleaseSpinlock(&sched_lock);
}

void SwitchToThread(struct thread* thr);

export void ReleaseScheduler(void) {
    if (ArchAreInterruptsEnabled()) {
        Panic(PANIC_FUCK_ME);
    }
    AcquireSpinlock(&sched_lock);
    bool zero = (--sched_prevent_count) == 0;
    if (zero && sched_postponed) {
        sched_postponed = false;
        struct thread* thr = sched_postponed_thr;
        sched_postponed_thr = NULL;
        SwitchToThread(thr);
    }
    ReleaseSpinlock(&sched_lock);
    if (zero) {
        ArchEnableInterrupts();
    }
}

export void PostMessageIrq(struct msg msg) {
    defer_msg = msg;
    has_defer_msg = true;
}

void ProcessIrqPostMessage(void) {
    static struct msg prev_mouse_msg;
    static bool has_prev_mouse_msg = true;
    extern struct msgbox* WmGetSystemMessageBox(void);
    struct msgbox* sysbox = WmGetSystemMessageBox();
    if (has_defer_msg) {
        AcquireScheduler();
        has_defer_msg = false;
        if (defer_msg.type == SYSMSG_MOUSEEVENT && has_prev_mouse_msg) {
            KeTryReplaceOrAdd(
                sysbox, 
                (const void*) &prev_mouse_msg,
                (const void*) &defer_msg, 
                TIMEOUT_INFINITE
            );
            
        } else {
            KePostMessage(sysbox, &defer_msg, TIMEOUT_INFINITE);
        }
        if (defer_msg.type == SYSMSG_MOUSEEVENT) {
            prev_mouse_msg = defer_msg;
            has_prev_mouse_msg = true;
        }
        ReleaseScheduler();
    }
}

static struct thread* FindNextThread(void) {
    if (ready_list_head == NULL) {
        Panic(PANIC_IDLE_TASK_HAS_BLOCKED);
    }
    struct thread* thr = ready_list_head;
    ready_list_head = ready_list_head->next_ready;
    if (thr == ready_list_tail) {
        ready_list_tail = NULL;
    }
    return thr;
}

static void AddToBackOfReadyQueue(struct thread* thr) {
    thr->state = THREAD_STATE_READY;
    thr->next_ready = NULL;
    if (ready_list_tail != NULL) {
        ready_list_tail->next_ready = thr;
    }
    ready_list_tail = thr;
    if (ready_list_head == NULL) {
        ready_list_head = thr;
    }
}

static void SetupInitialThread(void) {
    struct thread* thr = FindNextThread();
    current_thread = thr;
    current_thread->state = THREAD_STATE_RUNNING;
    LogPrintf("Switched to initial thread: 0x%X\n", current_thread);
}

void BeginNewThread(void) {
    bool zero = sched_prevent_count == 0;
    ReleaseSpinlock(&sched_lock);
    if (zero) {
        ArchEnableInterrupts();
    } else {
        Panic(PANIC_FUCK_ME);
    }
}

/* 
 * If `thr` is NULL, it will REMOVE the first element from the ready list and
 * run that.
 * 
 * If `thr` is NON-NULL, then it ought to already NOT BE on the list, and it
 * will switch to it.
 */
void SwitchToThread(struct thread* thr) {
    if (!sched_init) {
        return;
    }
    if (ArchAreInterruptsEnabled() || sched_prevent_count > 0 || sched_lock.lock != 1) {
        Panic(PANIC_FUCK_ME);
    }
    LogPrintf("Switch to thread: 0x%X\n", thr);
    if (current_thread->state == THREAD_STATE_RUNNING) {
        if (ready_list_head == NULL && thr == NULL) {
            /* Nothing else is available to run, so keep running. */
            return;
        }
        AddToBackOfReadyQueue(current_thread);
    }
    if (thr == NULL) {
        thr = FindNextThread();
        if (thr == current_thread) {
            LogPrintf("current thread was also on the queue...\n");
            Panic(PANIC_FUCK_ME);
        }
    }
    struct thread* old_thr = current_thread;
    current_thread = thr;
    current_thread->state = THREAD_STATE_RUNNING;
    current_thread->next_ready = NULL;
    /* TODO: set the VAS */

    LogPrintf("Actual switch: 0x%X -> 0x%X\n", old_thr, current_thread);
    ArchSwitchThread(old_thr, current_thread);

    /* NOTHING GOES HERE! We need the tail of the call chain to be as
     * little and invariant as possible, so new tasks can replicate it okay in 
     * `BeginNewThread` */
}

void BlockThread(void) {
    // Scheduler lock must already be held!
    GetCurrentThread()->state = THREAD_STATE_BLOCKED;
    GetCurrentThread()->block_return_val = 0;
    Schedule();
}

static bool ShouldPreempt(struct thread* thr) {
    return sched_init && (ready_list_head == NULL || thr->priority < GetCurrentThread()->priority);
}

/*
 * The RETV values here... 
 * 1 or more means an ERRNO code
 * 0 or negative means that it is semaphore number -N that returned. 
 */
void UnblockThread(struct thread* thr, int retv) {
    // Scheduler lock must already be held!
    LogPrintf("Unblock thread 0x%x... (retv %d)\n", thr, retv);
    if (thr->waiting_sem_or_clot != NULL) {
        CancelSems(thr);
    }
    thr->block_return_val = retv;
    thr->next_ready = NULL;
    thr->state = THREAD_STATE_READY;

    if (ShouldPreempt(thr)) {
        sched_postponed = true;
        if (sched_postponed_thr == NULL || thr->priority < sched_postponed_thr->priority) {
            LogPrintf("Postpone...\n");
            sched_postponed_thr = thr;
        }

    } else {
        AddToBackOfReadyQueue(thr);
    }
}

export void Schedule(void) {
    AcquireScheduler();
    sched_postponed = true;
    ReleaseScheduler();
}

void InitScheduler(void) {
    sched_prevent_count = 0;
    InitSpinlock(&sched_lock);
    CreateThread(GetKernelVas(), NULL, NULL);
    CreateThread(GetKernelVas(), IdleTask, NULL);
    sched_init = true;
    SetupInitialThread();
}
