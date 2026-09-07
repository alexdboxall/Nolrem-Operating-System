#include <common.h>
#include <scheduler.h>
#include <thread.h>
#include <spinlock.h>
#include <vmm.h>
#include <arch.h>
#include <panic.h>
#include <log.h>
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
static bool has_defer_msg = false;

export bool IsSchedulingInitialised(void) {
    return sched_init;
}

export struct thread* GetCurrentThread(void) {
    return current_thread;
}

export void AcquireScheduler(void) {
    asm ("cli");
    AcquireSpinlock(&sched_lock);
    sched_prevent_count++;
    ReleaseSpinlock(&sched_lock);
}

void SwitchToThread(struct thread* thr);

static inline bool are_interrupts_enabled()
{
    unsigned long flags;
    asm volatile ( "pushf\n\t"
                   "pop %0"
                   : "=g"(flags) );
    return flags & (1 << 9);
}


export void ReleaseScheduler(void) {
    if (are_interrupts_enabled()) {
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
        asm ("sti");
    }
}

export void PostMessageIrq(struct msg msg) {
    defer_msg = msg;
    has_defer_msg = true;
}

export void ProcessIrqPostMessage(void) {
    if (has_defer_msg) {
        has_defer_msg = false;
        extern struct msgbox* WmGetSystemMessageBox(void);
        LogPrintf("Handling deferred message...\n");

        sched_prevent_count++;
        KePostMessage(WmGetSystemMessageBox(), &defer_msg, -1);
        sched_prevent_count--;
        if (sched_prevent_count == 0 && sched_postponed) {
            sched_postponed = false;
            struct thread* thr = sched_postponed_thr;
            sched_postponed_thr = NULL;
            SwitchToThread(thr);
        }
    }
}

static struct thread* FindNextThread(void) {
    /*while (ready_list_head == NULL) {
        ReleaseScheduler();
        ArchIdle();
        AcquireScheduler();
    }*/
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
    LogPrintf("lalala\n");
    bool zero = sched_prevent_count == 0;
    ReleaseSpinlock(&sched_lock);
    if (zero) {
        asm ("sti");
    }
}

void SwitchToThread(struct thread* thr) {
    LogPrintf("Switching to thread! (0x%X)\n", thr);
    if (!sched_init) {
        return;
    }
    LogPrintf("Current thread is 0x%X\n", current_thread);
    if (current_thread->state == THREAD_STATE_RUNNING) {
        if (ready_list_head == NULL) {
            LogPrintf("There's nothing else to run!\n");
            /* Nothing else is available to run, so keep running. */
            return;
        }
        LogPrintf("About to add the current thread to the back of the ready queue?\n");
        AddToBackOfReadyQueue(current_thread);
    }
    LogPrintf("Current thread is in state: 0x%X -> %d\n", current_thread, current_thread->state);
    if (thr == NULL) {
        thr = FindNextThread();
    }
    struct thread* old_thr = current_thread;
    current_thread = thr;
    current_thread->state = THREAD_STATE_RUNNING;
    current_thread->next_ready = NULL;
        /* TODO: set the VAS */

    LogPrintf("Switching: 0x%X -> 0x%X\n", old_thr, current_thread);
    ArchSwitchThread(old_thr, current_thread);

    /* NOTHING GOES HERE! We need the tail of the call chain to be as
     * little and invariant as possible, so new tasks can replicate it okay in 
     * `BeginNewThread` */
}

void BlockThread(void) {
    // Scheduler lock must already be held!
    LogPrintf("Blocking 0x%X\n", GetCurrentThread());
    GetCurrentThread()->state = THREAD_STATE_BLOCKED;
    GetCurrentThread()->block_return_val = 0;
    LogPrintf("Scheduling due to block...\n");
    Schedule();
}

static bool ShouldPreempt(struct thread* thr) {
    return sched_init && (ready_list_head == NULL || thr->priority < GetCurrentThread()->priority);
}

void UnblockThread(struct thread* thr, int retv) {
    // Scheduler lock must already be held!
    // TODO: add back to the list
    // TODO: sort out semaphore cancellation, etc.
    thr->block_return_val = retv;
 
    // This has to happen after all sem / timer cancellations done

    thr->next_ready = NULL;
    thr->next_waiting_timer = NULL;
    thr->state = THREAD_STATE_READY;

    if (false && ShouldPreempt(thr)) {
        LogPrintf("Preempting?\n");
        if (sched_prevent_count != 0) {
            sched_postponed = true;
            if (sched_postponed_thr == NULL || thr->priority < sched_postponed_thr->priority) {
                sched_postponed_thr = thr;
            }
            
        } else {
            SwitchToThread(thr);
        }

    } else {
        LogPrintf("Adding to the back of the queue...\n");
        AddToBackOfReadyQueue(thr);
    }
}

export void Schedule(void) {
    AcquireScheduler();
    sched_postponed = true;
    ReleaseScheduler();
}

void InitScheduler(void(*entry)(void*)) {
    sched_prevent_count = 0;
    InitSpinlock(&sched_lock);
    LogPrintf("About to create kernel thread...\n");
    struct thread* krnl_thr = CreateThread(GetKernelVas(), entry, NULL);
    CreateThread(GetKernelVas(), IdleTask, NULL);
    LogPrintf("About to create idle thread...\n");
    (void) krnl_thr;
    LogPrintf("About to switch to kernel thread...\n");
    sched_init = true;
    SetupInitialThread();
}
