#include <common.h>
#include <thread.h>
#include <obj.h>
#include <scheduler.h>
#include <heap.h>
#include <vmm.h>
#include <arch.h>
#include <log.h>

#define KSTACK_SIZE     (1024 * 4)
#define USTACK_SIZE     (1024 * 16)

export size_t KeGetCurrentThreadKernelStackRemaining(void) {
    struct thread* thr = KeGetCurrentThread();
    size_t stack_current = (size_t) &thr;
    size_t stack_bottom = thr->kernel_stack_top - thr->kernel_stack_size;
    return stack_current - stack_bottom;
}

export void KeSetLastUserError(int err) {
    KeGetCurrentThread()->last_error = err;
}

export struct thread* KeCreateThreadEx(struct vas* vas, void(*entry)(void*), void* context, size_t kstack_size) {
    struct thread* thr = AllocHeap(sizeof(struct thread));
    if (thr == NULL) {
        return NULL;
    }
    KeInitObject(thr, OBJTYPE_THREAD);

    thr->vas = vas;

    if (entry == NULL) {
        /*
         * The first kernel task continues to use its own special stack.
         * Luckily, it won't ever be terminated, so we don't need to worry about
         * cleanup of the not-through-the-VMM stack. 
         * 
         * Set to dummy values so if one day we get a crash related to this, we
         * can tell the reason.
         */
        thr->kernel_stack_top = 0xBEEFBEAD;
        thr->kernel_stack_size = 0xDEADBEEF;

    } else {
        thr->kernel_stack_size = kstack_size;
        size_t kernel_stack_bottom;
        if (kstack_size < PAGE_SIZE) {
            kernel_stack_bottom = (size_t) AllocHeap(thr->kernel_stack_size);
            if (kernel_stack_bottom == 0) {
                FreeHeap(thr);
                return NULL;
            }
        } else {
            kernel_stack_bottom = (size_t) KeAllocAnonMemory(thr->kernel_stack_size, VP_WRITE);
        }
        thr->kernel_stack_top = kernel_stack_bottom + thr->kernel_stack_size;
    }
    
    thr->next_ready = NULL;
    thr->flags = 0;
    thr->next_waiting_timer = NULL;
    thr->next_waiting_sem = NULL;
    thr->waiting_sem_or_clot = NULL;
    thr->priority = PRIORITY_NORMAL;
    thr->block_return_val = 0;
    thr->wakeup_time_ns = 0;
    thr->affinity_bitmask = -1;     /* all processors */
    thr->idling = false;
    thr->irq_disable_counter = 2;   /* BeginThread does TWO pops to IF state */
    thr->saved_interrupt_enabled = true;
    
    if (vas == KeGetKernelVas()) {
        thr->stack_pointer = thr->kernel_stack_top;
    } else {
        thr->user_stack_base = (size_t) KeAllocAnonMemory(USTACK_SIZE, VP_WRITE | VP_USER);
        thr->stack_pointer = thr->user_stack_base + USTACK_SIZE;
    }

    if (entry != NULL) {
        /* Skip for initial kernel thread, we're not really entering it. */
        ArchSetupNewThreadEntry(thr, entry, context);
    }
    KeAcquireScheduler();
    KeUnblockThread(thr, 0);
    KeReleaseScheduler();

    return thr;
}

export struct thread* KeCreateThread(struct vas* vas, void(*entry)(void*), void* context) {
    return KeCreateThreadEx(vas, entry, context, KSTACK_SIZE);
}

export uint8_t KeGetThreadPriority(struct thread* thr) {
    KeAcquireScheduler();
    uint8_t retv = thr->priority;
    KeReleaseScheduler();
    return retv;
}

export void KeSetThreadPriority(struct thread* thr, uint8_t priority) {
    KeAcquireScheduler();
    thr->priority = priority;
    KeReleaseScheduler();
}

static void CleanupThread(void* _thr) {
    struct thread* thr = _thr;
    FreeHeap(thr);
}

void KeStartDelayTermination(void) {
    KeAcquireScheduler();
    /* TODO: probably some process-side things too once that's implemented */
    struct thread* thr = KeGetCurrentThread();
    KeRefObject(thr);
    thr->flags |= TF_DELAY_TERMINATE;
    KeReleaseScheduler();
}

void KeEndDelayTermination(void) {
     KeAcquireScheduler();
    /* TODO: probably some process-side things too once that's implemented */
    struct thread* thr = KeGetCurrentThread();
    thr->flags &= ~TF_DELAY_TERMINATE;
    if (thr->flags & TF_MARKED_TERMINATION) {
        // TODO: terminate however that's done
    }
    KeDerefObject(thr);
    KeReleaseScheduler();
}

void KeInitThread(void) { 
    KeRegisterObjectType(OBJTYPE_THREAD, CleanupThread);
}