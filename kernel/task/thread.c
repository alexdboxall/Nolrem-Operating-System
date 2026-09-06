#include <common.h>
#include <thread.h>
#include <obj.h>
#include <scheduler.h>
#include <heap.h>
#include <vmm.h>
#include <log.h>

#define KSTACK_SIZE     (1024 * 8)
#define USTACK_SIZE     (1024 * 16)

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

export struct thread* CreateThread(struct vas* vas, void(*entry)(void*), void* context) {
    struct thread* thr = AllocHeap(sizeof(struct thread));
    InitObject(thr, OBJTYPE_THREAD);
    thr->state = THREAD_STATE_READY;

    thr->vas = vas;
    thr->kernel_stack_size = KSTACK_SIZE;
    size_t kernel_stack_bottom = (size_t) AllocAnonMemory(thr->kernel_stack_size, VP_WRITE);
    thr->kernel_stack_top = kernel_stack_bottom + thr->kernel_stack_size;
    thr->next_waiting_sem = NULL;
    thr->next_waiting_timer = NULL;
     
    if (vas == GetKernelVas()) {
        thr->stack_pointer = thr->kernel_stack_top;
    } else {
        struct virt_page* vp = CreateVirtPage(vas, AllocVirtEx(vas->va, USTACK_SIZE), VP_USER | VP_WRITE, NULL, 0, 0, 0);
        if (vp == NULL) {
            LogString("CreateThread: CreateVirtPage for stack is NULL!\n");
            while (true) {;}
        }
        thr->user_stack_base = vp->virt;
        thr->stack_pointer = thr->user_stack_base + USTACK_SIZE;
    }

    (void) entry;
    (void) context;

    return thr;
}

static void CleanupThread(void* _thr) {
    struct thread* thr = _thr;
    FreeHeap(thr);
}

void InitThread(void) { 
    current_thread = &dummy;
    RegisterObjectType(OBJTYPE_THREAD, CleanupThread);
}