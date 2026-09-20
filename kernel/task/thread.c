#include <common.h>
#include <thread.h>
#include <obj.h>
#include <scheduler.h>
#include <heap.h>
#include <vmm.h>
#include <arch.h>
#include <log.h>

#define KSTACK_SIZE     (1024 * 8)
#define USTACK_SIZE     (1024 * 16)

export struct thread* CreateThread(struct vas* vas, void(*entry)(void*), void* context) {
    struct thread* thr = AllocHeap(sizeof(struct thread));
    InitObject(thr, OBJTYPE_THREAD);

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
        thr->kernel_stack_size = entry == IdleTask ? PAGE_SIZE : KSTACK_SIZE;
        size_t kernel_stack_bottom = (size_t) AllocAnonMemory(thr->kernel_stack_size, VP_WRITE);
        thr->kernel_stack_top = kernel_stack_bottom + thr->kernel_stack_size;
    }
    
    thr->next_ready = NULL;
    thr->next_waiting_timer = NULL;
    thr->next_waiting_sem = NULL;
    thr->waiting_sem_or_clot = NULL;
    thr->priority = PRIORITY_NORMAL;
    thr->block_return_val = 0;

    if (vas == GetKernelVas()) {
        thr->stack_pointer = thr->kernel_stack_top;
    } else {
        // TODO: need to get this to allocate multiple virt pages 
        struct virt_page* vp = CreateVirtPage(vas, AllocVirtEx(vas->va, USTACK_SIZE), VP_USER | VP_WRITE, NULL, 0, 0, 0, false);
        if (vp == NULL) {
            LogString("CreateThread: CreateVirtPage for stack is NULL!\n");
            while (true) {;}
        }
        thr->user_stack_base = vp->virt;
        thr->stack_pointer = thr->user_stack_base + USTACK_SIZE;
    }

    if (entry != NULL) {
        /* Skip for initial kernel thread, we're not really entering it. */
        ArchSetupNewThreadEntry(thr, entry, context);
    }
    AcquireScheduler();
    UnblockThread(thr, 0);
    ReleaseScheduler();

    return thr;
}

export uint8_t GetThreadPriority(struct thread* thr) {
    AcquireScheduler();
    uint8_t retv = thr->priority;
    ReleaseScheduler();
    return retv;
}

export void SetThreadPriority(struct thread* thr, uint8_t priority) {
    AcquireScheduler();
    thr->priority = priority;
    ReleaseScheduler();
}

static void CleanupThread(void* _thr) {
    struct thread* thr = _thr;
    FreeHeap(thr);
}

void InitThread(void) { 
    RegisterObjectType(OBJTYPE_THREAD, CleanupThread);
}