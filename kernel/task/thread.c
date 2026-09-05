#include <common.h>
#include <thread.h>
#include <obj.h>
#include <scheduler.h>
#include <heap.h>

#define KSTACK_SIZE     (4096 * 2)

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

struct thread* CreateThread(void(*entry)(void*), void* context) {
    struct thread* thr = AllocHeap(sizeof(struct thread));
    InitObject(thr, OBJTYPE_THREAD);
    thr->state = THREAD_STATE_READY;

    // TODO: need to alloc virt
    //thr->kernel_stack_top = 
    thr->kernel_stack_size = KSTACK_SIZE;

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