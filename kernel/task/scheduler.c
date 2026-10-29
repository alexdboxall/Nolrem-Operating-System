#include <common.h>
#include <scheduler.h>
#include <thread.h>
#include <spinlock.h>
#include <vmm.h>
#include <errno.h>
#include <arch.h>
#include <panic.h>
#include <log.h>
#include <arch.h>
#include <msgbox.h>
#include <timer.h>
#include <interrupt.h>
#include <cpu.h>
#include <stdatomic.h>

/*
 * REQUIRES in <thread.h>:
 *     bool idling;
 * inside `struct thread`. It must be initialised to false in CreateThread
 * (and is protected by `sched_lock`).
 *
 * `idling` means: some CPU is parked in `IdleUntilRunnable` on this thread's
 * stack. While it is set the thread must NEVER be put on the ready queue (or
 * any other CPU could start running it on top of the idling CPU). If it gets
 * unblocked it is just marked READY, and the idling CPU resumes it itself.
 */

static _Atomic uint32_t something_unblocked_seq = 0;
static struct spinlock sched_lock;
static int sched_prevent_count = 0;
static bool sched_postponed = false;
static struct thread* sched_postponed_thr = NULL;
static struct thread* ready_list_head = NULL;
static struct thread* ready_list_tail = NULL;
static struct thread* current_thread = NULL;
static bool sched_init = false;
static bool has_defer_msg = 0;

/* Used so the low priority bg task can be more aggro when it's been running for a while */
volatile uint64_t last_task_switch_time = 0;

bool IsSchedulingInitialised(void) {
    return sched_init;
}

export struct thread* GetCurrentThread(void) {
    return current_thread;
}

export void AcquireScheduler(void) {
    AcquireIrqPrevent();
    AcquireSpinlock(&sched_lock);
    sched_prevent_count++;
    ReleaseSpinlock(&sched_lock);
}

static bool CanThreadRunOnThisCpu(struct thread* thr) {
    int cpu_num = ArchGetCpuNum();
    return !!(thr->affinity_bitmask & (1ULL << cpu_num));
}

/*
 * Remove and return the first thread on the ready queue that is allowed to run
 * on this CPU, or NULL if there is none. Scheduler lock must be held.
 */
static struct thread* FindNextThread(void) {
    struct thread* prev = NULL;
    struct thread* thr = ready_list_head;
    while (thr != NULL) {
        if (CanThreadRunOnThisCpu(thr)) {
            /* Unlink `thr`. */
            if (prev == NULL) {
                ready_list_head = thr->next_ready;
            } else {
                prev->next_ready = thr->next_ready;
            }
            if (thr == ready_list_tail) {
                /* `prev` is NULL if `thr` was the only element. */
                ready_list_tail = prev;
            }
            thr->next_ready = NULL;
            return thr;
        }
        prev = thr;
        thr = thr->next_ready;
    }
    return NULL;
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

/* Is this CPU currently parked in the idle loop (on current_thread's stack)? */
static bool IsCurrentThreadIdling(void) {
    return sched_init && current_thread != NULL && current_thread->idling;
}

void SwitchToThread(struct thread* thr);

export void ReleaseScheduler(void) {
    if (ArchAreInterruptsEnabled()) {
        LogPrintf("A\n");
        Panic(PANIC_FUCK_ME);
    }
    AcquireSpinlock(&sched_lock);
    bool zero = (--sched_prevent_count) == 0;
    if (zero && sched_postponed) {
        sched_postponed = false;
        struct thread* thr = sched_postponed_thr;
        sched_postponed_thr = NULL;
        if (IsCurrentThreadIdling()) {
            /* We are an IRQ that landed inside the idle loop, running on the
             * idling thread's stack. Do NOT switch from here: the idle loop
             * does the picking. Just make sure anything postponed is visible
             * to it, and kick it so it re-checks. */
            if (thr != NULL) {
                AddToBackOfReadyQueue(thr);
                ++something_unblocked_seq;
            }
        } else {
            SwitchToThread(thr);
        }
    }
    ReleaseSpinlock(&sched_lock);
    ReleaseIrqPrevent();
}

struct msg irq_msg_queue[32];

// Both bitmaps start at 0 (all slots free, no slots filled)
_Atomic uint32_t irq_msg_claimed = 0;
_Atomic uint32_t irq_msg_filled = 0;

static int EnqueueIrqMessage(struct msg msg) {
    uint32_t current = atomic_load_explicit(&irq_msg_claimed, memory_order_relaxed);
    
    while (true) {
        if (current == UINT32_MAX) {
            return ENOBUFS; // Queue completely full
        }
        
        // Find the first zero bit (free slot)
        int bit_index = __builtin_ctz(~current);
        if (bit_index >= 32) return ENOBUFS; // Safety boundary check
        
        uint32_t desired = current | (1U << bit_index);
        
        // Attempt to atomically claim/reserve this specific bit
        if (atomic_compare_exchange_weak_explicit(&irq_msg_claimed, &current, desired,
                                                 memory_order_relaxed,
                                                 memory_order_relaxed)) {
            // 1. We successfully claimed slot 'bit_index'. Nobody else can touch it.
            irq_msg_queue[bit_index] = msg;
            
            // 2. Safely publish to consumers that data is ready.
            // memory_order_release ensures the payload write finishes BEFORE the bit flips.
            atomic_fetch_or_explicit(&irq_msg_filled, (1U << bit_index), memory_order_release);
            return 0;
        }
        // If CAS failed, 'current' is automatically updated with the new snapshot; loop tries again.
    }
}

static int DequeueIrqMessage(struct msg* msg_out) {
    if (msg_out == NULL) {
        return EINVAL;
    }
    
    uint32_t current = atomic_load_explicit(&irq_msg_filled, memory_order_relaxed);
    
    while (true) {
        if (current == 0) {
            return EAGAIN; // Queue completely empty
        }
        
        // Find the first set bit (available slot)
        // Note: Using ctz (Count Trailing Zeros) to match the producer's order.
        int bit_index = __builtin_ctz(current);
        if (bit_index >= 32) return EAGAIN;
        
        uint32_t desired = current & ~(1U << bit_index);
        
        // Attempt to atomically claim this filled slot for reading
        // memory_order_acquire ensures we don't read the array until we successfully win this bit
        if (atomic_compare_exchange_weak_explicit(&irq_msg_filled, &current, desired,
                                                 memory_order_acquire,
                                                 memory_order_relaxed)) {
            // 1. We successfully claimed this filled slot. Copy data out.
            *msg_out = irq_msg_queue[bit_index];
            
            // 2. Free up the slot in the 'claimed' bitmap so producers can reuse it.
            // memory_order_release ensures the read finishes BEFORE the slot is re-opened.
            atomic_fetch_and_explicit(&irq_msg_claimed, ~(1U << bit_index), memory_order_release);
            return 0;
        }
        // If CAS failed, someone else grabbed this message; 'current' updates, loop retries.
    }
}

export void PostMessageIrq(struct msg msg) {
    EnqueueIrqMessage(msg);
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
        struct msg defer_msg;
        while (DequeueIrqMessage(&defer_msg) == 0) {
            if (defer_msg.type == SYSMSG_MOUSEEVENT && has_prev_mouse_msg) {
                KeTryReplaceOrAdd(
                    sysbox, 
                    (const void*) &prev_mouse_msg,
                    (const void*) &defer_msg, 
                    TIMEOUT_INSTANT
                );
                
            } else {
                KePostMessage(sysbox, &defer_msg, TIMEOUT_INSTANT);
            }
            if (defer_msg.type == SYSMSG_MOUSEEVENT) {
                prev_mouse_msg = defer_msg;
                has_prev_mouse_msg = true;
            }
        }
        ReleaseScheduler();
    }
}

static void SetupInitialThread(void) {
    struct thread* thr = FindNextThread();
    current_thread = thr;
    current_thread->state = THREAD_STATE_RUNNING;
}

void BeginNewThread(void) {
    bool zero = sched_prevent_count == 0;
    ReleaseSpinlock(&sched_lock);
    if (zero) {
        ReleaseIrqPrevent();
    } else {
        LogPrintf("B\n");
        Panic(PANIC_FUCK_ME);
    }
}

/*
 * Idle this CPU on the (BLOCKED) current thread's own stack until something is
 * runnable. Costs no extra stack.
 *
 * Called with `sched_lock` held and interrupts off; returns the same way.
 * Returns either:
 *   - `current_thread` (it was unblocked while we idled; it is NOT on the
 *     ready queue, because UnblockThread never queues an idling thread), or
 *   - a thread freshly removed from the ready queue.
 */
static struct thread* IdleUntilRunnable(void) {
    struct thread* idler = current_thread;
    struct thread* thr;

    idler->idling = true;
    while (true) {
        /* Read under the lock, after our last FindNextThread: any unblock
         * after this point must take the lock, so it will bump the seq. */
        uint32_t og_seq = something_unblocked_seq;
        ReleaseSpinlock(&sched_lock);
        ArchEnableInterrupts();
        do {
            ArchIdle();
        } while (something_unblocked_seq == og_seq);
        ArchDisableInterrupts();
        AcquireSpinlock(&sched_lock);

        /* Prefer resuming ourselves: we're already off the queue, so no queue
         * fixup is needed, and no other CPU can have touched us. */
        if (idler->state == THREAD_STATE_READY) {
            thr = idler;
            break;
        }
        thr = FindNextThread();
        if (thr != NULL) {
            break;
        }
    }
    idler->idling = false;
    return thr;
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
    if (current_thread->state == THREAD_STATE_RUNNING) {
        if (ready_list_head == NULL && thr == NULL) {
            /* Nothing else is available to run, so keep running. */
            return;
        }
        AddToBackOfReadyQueue(current_thread);
    }
    if (thr == NULL) {
        thr = FindNextThread();
        if (thr == NULL) {
            if (current_thread->state != THREAD_STATE_BLOCKED) {
                /* Should be unreachable: if the current thread was RUNNING it
                 * was just queued, and it can obviously run on this CPU, so
                 * FindNextThread would at worst have returned it. We can't
                 * idle on a stack that's sitting on the ready queue. */
                LogPrintf("SwitchToThread: current thread in unexpected state\n");
                Panic(PANIC_FUCK_ME);
            }
            thr = IdleUntilRunnable();
        }
        /* This is allowed: either we were blocked then woken (e.g. by a timer)
         * before we got around to switching away, or we were RUNNING and every
         * other queued thread is pinned to other CPUs, so we just keep going. */
        if (thr == current_thread) {
            thr->state = THREAD_STATE_RUNNING;
            thr->next_ready = NULL;
            return;
        }
    } else {
        if (!CanThreadRunOnThisCpu(thr)) {
            LogPrintf("SwitchToThread: called with a thread with wrong affinity\n");
            Panic(PANIC_FUCK_ME);
        }
    }

    struct thread* old_thr = current_thread;
    current_thread = thr;
    current_thread->state = THREAD_STATE_RUNNING;
    current_thread->next_ready = NULL;

    last_task_switch_time = GetTimeSinceBoot();
    /* TODO: set the VAS */

    //LogPrintf("Actual switch: 0x%X -> 0x%X\n", old_thr, current_thread);
    struct cpu_data* cpu = GetCpu();
    old_thr->irq_disable_counter     = cpu->irq_disable_counter;   /* per-thread copies in struct thread */
    old_thr->saved_interrupt_enabled = cpu->saved_interrupt_enabled;
    cpu->irq_disable_counter         = thr->irq_disable_counter;
    cpu->saved_interrupt_enabled     = thr->saved_interrupt_enabled;

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
    if (!sched_init) {
        return false;
    }
    if (GetCurrentThread()->state != THREAD_STATE_RUNNING) {
        /* Current is blocked / on its way out: there is nothing to preempt, 
         * so the woken thread should just be queued. */
        return false;
    }
    if (!CanThreadRunOnThisCpu(thr)) {
        /* Can't switch to it from here; queue it for a CPU that can. */
        return false;
    }
    return ready_list_head == NULL || thr->priority < GetCurrentThread()->priority;
}

/*
 * The RETV values here... 
 * 1 or more means an ERRNO code
 * 0 or negative means that it is semaphore number -N that returned. 
 */
void UnblockThread(struct thread* thr, int retv) {
    // Scheduler lock must already be held!
    if (thr->waiting_sem_or_clot != NULL) {
        CancelSems(thr);
    }
    if (thr->wakeup_time_ns != 0) {
        CancelTimer(thr);
    }
    thr->block_return_val = retv;
    thr->next_ready = NULL;
    thr->state = THREAD_STATE_READY;

    /* TODO: may need some sort of IPI thing in the future somewhere in this
     * func to wake other idling CPUs.
     */
    if (thr->idling) {
        /* A CPU is parked on this thread's stack. Queueing it would let another
         * CPU run it on top of the idler, so leave it off the queue; it is now
         * marked READY and the idling CPU will notice and resume it. */
        ++something_unblocked_seq;
        return;
    }

    if (ShouldPreempt(thr)) {
        sched_postponed = true;
        if (sched_postponed_thr == NULL) {
            sched_postponed_thr = thr;
        } else if (thr->priority < sched_postponed_thr->priority) {
            /* Replace `sched_postponed_thr` with something even higher
             * priority, so put the old `sched_postponed_thr` on the queue so it
             * isn't dropped completely! */
            AddToBackOfReadyQueue(sched_postponed_thr);
            sched_postponed_thr = thr;
        } else {
            AddToBackOfReadyQueue(thr);
        }
    } else {
        AddToBackOfReadyQueue(thr);
    }
    ++something_unblocked_seq;
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
    sched_init = true;
    SetupInitialThread();
}
