#include <obj.h>
#include <heap.h>
#include <common.h>
#include <sem.h>
#include <errno.h>
#include <thread.h>
#include <scheduler.h>
#include <log.h>
#include <mutex.h>

static void CleanupSem(void* _sem) {
    struct sem* sem = _sem;
    FreeHeap(sem);
}

void DestroyStaticSem(struct sem* sem) {
    (void) sem;
}

void InitSem(void) {
    RegisterObjectType(OBJTYPE_SEM, CleanupSem);
}

export void PrintSemCount(struct sem* sem) {
    LogPrintf("%d", sem->count);
}

export void InitStaticSem(struct sem* sem, int max, int initial) {
    sem->count = initial;
    sem->max = max;
    sem->waiting_list_start = NULL;
    sem->waiting_list_end = NULL;
}

export struct sem* CreateSem(int max, int initial) {
    struct sem* sem = AllocHeap(sizeof(struct sem));
    InitObject(sem, OBJTYPE_SEM);
    InitStaticSem(sem, max, initial);
    return sem;
}

export struct mutex* CreateMutex(void) {
    return (struct mutex*) CreateSem(1, 0);
}

export int AcquireSem(struct sem* sem, int64_t timeout) {
    if (!IsSchedulingInitialised()) {
        sem->count++;
        return 0;
    }

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

        if (sem->waiting_list_end != NULL) {
            sem->waiting_list_end->next_waiting_sem = curr_thr;
        }
        sem->waiting_list_end = curr_thr;
        if (sem->waiting_list_start == NULL) {
            sem->waiting_list_start = curr_thr;
        }
        curr_thr->next_waiting_sem = NULL;

        // TODO: add to sleep queue if needed
        
        BlockThread();
    }

    ReleaseScheduler();

    // TODO: any cleanup needed here...?
    SetThreadWaitingSem(curr_thr, NULL);

    return curr_thr->block_return_val;
}

export int ReleaseSem(struct sem* sem) {
    if (!IsSchedulingInitialised()) {
        sem->count--;
        return 0;
    }

    AcquireScheduler();

    if (sem->count == sem->max) {
        if (sem->waiting_list_start == NULL) {
            sem->count--;
        } else {
            struct thread* thr = sem->waiting_list_start;
            sem->waiting_list_start = thr->next_waiting_sem;
            UnblockThread(thr, 0);
        }

    } else {
        sem->count--;
    }
    ReleaseScheduler();
    return 0;
}


export void InitStaticMutex(struct mutex* mtx) {
    InitStaticSem(&mtx->sem, 1, 0);
}

export void DestroyStaticMutex(struct mutex* mtx) {
    DestroyStaticSem(&mtx->sem);
}


export int AcquireMutex(struct mutex* mtx, int64_t timeout) {
    return AcquireSem((struct sem*) mtx, timeout);
}

export int ReleaseMutex(struct mutex* mtx) {
    return ReleaseSem((struct sem*) mtx);
}