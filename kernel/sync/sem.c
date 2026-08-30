#include <obj.h>
#include <heap.h>
#include <common.h>
#include <sem.h>
#include <errno.h>
#include <thread.h>
#include <scheduler.h>

struct sem {
    struct obj_header hdr;
    int count;
    int max;
    struct thread* waiting_list_start;
    struct thread* waiting_list_end;
};

struct mutex {
    struct sem sem;
};

static void CleanupSem(void* _sem) {
    struct sem* sem = _sem;
    FreeHeap(sem);
}

void InitSem(void) {
    RegisterObjectType(OBJTYPE_SEM, CleanupSem);
}

export struct sem* CreateSem(int max) {
    struct sem* sem = AllocHeap(sizeof(struct sem));
    InitObject(sem, OBJTYPE_SEM);
    sem->count = 0;
    sem->max = max;
    sem->waiting_list_start = NULL;
    sem->waiting_list_end = NULL;
    return sem;
}

export struct mutex* CreateMutex(void) {
    return (struct mutex*) CreateSem(1);
}

export int AcquireSem(struct sem* sem, int64_t timeout) {
    AcquireScheduler();

    struct thread* curr_thr = GetCurrentThread();
    if (sem->count < sem->max) {
        sem->count++;
    } else {
        if (timeout == TIMEOUT_INSTANT) {
            ReleaseScheduler();
            return EAGAIN;
        }

        SetThreadWaitingSem(curr_thr, sem);
        (void) timeout;
        // TODO: add to sleep queue if needed
        
        BlockThread();
        Schedule();
    }
    ReleaseScheduler();
    // TODO: any cleanup needed here...?
    SetThreadWaitingSem(curr_thr, NULL);
    return curr_thr->block_return_val;
}

export int ReleaseSem(struct sem* sem) {
    AcquireScheduler();
    if (sem->count == sem->max) {
        // TODO: find a thread and wake it
    } else {
        sem->count--;
    }
    ReleaseScheduler();
    return ENOSYS;
}

export int AcquireMutex(struct mutex* mtx, int64_t timeout) {
    return AcquireSem((struct sem*) mtx, timeout);
}

export int ReleaseMutex(struct mutex* mtx) {
    return ReleaseSem((struct sem*) mtx);
}