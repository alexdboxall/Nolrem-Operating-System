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

export struct thread* CreateThread(struct vas* vas, void(*entry)(void*), void* context) {
    LogString("A\n");
    struct thread* thr = AllocHeap(sizeof(struct thread));
        LogString("B\n");

    InitObject(thr, OBJTYPE_THREAD);
    LogString("C\n");

    thr->vas = vas;
    thr->kernel_stack_size = KSTACK_SIZE;
    size_t kernel_stack_bottom = (size_t) AllocAnonMemory(thr->kernel_stack_size, VP_WRITE);
    thr->kernel_stack_top = kernel_stack_bottom + thr->kernel_stack_size;
    thr->next_ready = NULL;
    thr->next_waiting_timer = NULL;
    thr->priority = PRIORITY_NORMAL;
    thr->waiting_sem = NULL;
    LogString("D\n");

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

    LogPrintf("About to ArchSetupNewThreadEntry...\n thr is at 0x%X", thr);
    ArchSetupNewThreadEntry(thr, entry, context);
    LogPrintf(".\n");
    AcquireScheduler();
    LogPrintf("About to unblock thread... 0x%X\n", thr);
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