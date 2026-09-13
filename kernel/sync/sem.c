#include <obj.h>
#include <heap.h>
#include <common.h>
#include <sem.h>
#include <errno.h>
#include <thread.h>
#include <scheduler.h>
#include <log.h>
#include <string.h>
#include <mutex.h>

static void CleanupSem(void* _sem) {
    struct sem* sem = _sem;
    FreeHeap(sem);
}

static void CleanupWaitClot(void* _clot) {
    FreeHeap(_clot);
}

void DestroyStaticSem(struct sem* sem) {
    (void) sem;
}

void InitSem(void) {
    RegisterObjectType(OBJTYPE_SEM, CleanupSem);
    RegisterObjectType(OBJTYPE_WAITCLOT, CleanupWaitClot);
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

struct wait_clot {
    struct obj_header hdr;
    struct thread* thr;
    int count;
    struct sem** sems;
    void** nexts;
};

static struct wait_clot* CreateClot(struct sem** sems, int count, struct thread* thr) {
    struct wait_clot* clot = AllocHeap(sizeof(struct wait_clot));
    InitObject(clot, OBJTYPE_WAITCLOT);
    clot->count = count;
    clot->thr = thr;
    clot->sems = AllocHeap(sizeof(struct sem*) * count);
    clot->nexts = AllocHeap(sizeof(void*) * count);
    memset(clot->nexts, 0, sizeof(void*) * count);
    memcpy(clot->sems, sems, sizeof(struct sem*) * count);
    return clot;
}

static int LookupClotIndex(struct wait_clot* clot, struct sem* sem) {
    for (int i = 0; i < clot->count; ++i) {
        if (clot->sems[i] == sem) {
            return i;
        }
    }
    return -1;
}

static void AddThreadToSemQueues(struct thread* thr, struct sem** sems, int count) {
    void* to_add;
    if (count > 1) {
        to_add = CreateClot(sems, count, thr);
    } else {
        to_add = thr;
    }

    thr->next_waiting_sem = NULL;
    thr->waiting_sem_or_clot = count > 1 ? to_add : sems[0];

    for (int i = 0; i < count; ++i) {
        struct sem* sem = sems[i];

        /* Point the existing last thing to the newly added last thing. */
        if (sem->waiting_list_end != NULL) {
            if (((struct obj_header*) sem->waiting_list_end)->objtype == OBJTYPE_THREAD) {
                ((struct thread*) sem->waiting_list_end)->next_waiting_sem = to_add;
            } else {
                struct wait_clot* clot = sem->waiting_list_end;
                int index = LookupClotIndex(clot, sem);
                clot->nexts[index] = to_add;
            }
        }
        sem->waiting_list_end = to_add;
        if (sem->waiting_list_start == NULL) {
            sem->waiting_list_start = to_add;
        }
    }
}

static void CleanupAfterSem(struct sem** sems, int count, struct thread* thr) {
    for (int i = 0; i < count; ++i) {
        struct sem* sem = sems[i];

        uint8_t head_hdr_type = ((struct obj_header*) sem->waiting_list_start)->objtype;
        if (head_hdr_type == OBJTYPE_THREAD && ((struct thread*) sem->waiting_list_start) == thr) {
            /* Remove from head. */
            sem->waiting_list_start = ((struct thread*) sem->waiting_list_start)->next_waiting_sem;
            if (sem->waiting_list_start == NULL) {
                sem->waiting_list_end = NULL;
            }

        } else if (head_hdr_type == OBJTYPE_WAITCLOT && ((struct wait_clot*) sem->waiting_list_start)->thr == thr) {
            /* Remove from head. */
            struct wait_clot* clot = sem->waiting_list_start;
            int index = LookupClotIndex(clot, sem);
            sem->waiting_list_start = clot->nexts[index];
            if (sem->waiting_list_start == NULL) {
                sem->waiting_list_end = NULL;
            }

        } else {
            /* It's not the head, so we can start on element 2 and have a prev*/
            void* prev = sem->waiting_list_start;
            void* curr;
            if (head_hdr_type == OBJTYPE_THREAD) {
                curr = ((struct thread*) prev)->next_waiting_sem;
            } else {
                struct wait_clot* clot = prev;
                int index = LookupClotIndex(clot, sem);
                curr = clot->nexts[index];
            }
            while (curr != NULL) {
                struct wait_clot* curr_as_clot = curr;
                struct thread* curr_as_thr = curr;
                bool curr_is_clot = ((struct obj_header*) curr)->objtype == OBJTYPE_WAITCLOT;
                struct wait_clot* prev_as_clot = prev;
                struct thread* prev_as_thr = prev;
                bool prev_is_clot = ((struct obj_header*) prev)->objtype == OBJTYPE_WAITCLOT;
                int curr_clot_idx = curr_is_clot ? LookupClotIndex(curr_as_clot, sem) : -1;
                int prev_clot_idx = prev_is_clot ? LookupClotIndex(prev_as_clot, sem) : -1;

                if ((!curr_is_clot && curr_as_thr == thr) || (curr_is_clot && curr_as_clot->thr == thr)) {
                    void* rhs = curr_is_clot ? curr_as_clot->nexts[curr_clot_idx] : curr_as_thr->next_waiting_sem;
                    void** lhs = curr_is_clot 
                        ? (void**) &(prev_as_clot->nexts[prev_clot_idx])
                        : (void**) &(prev_as_thr->next_waiting_sem)
                    ;
                    *lhs = rhs;
                    if (curr == sem->waiting_list_end) {
                        sem->waiting_list_end = prev;
                    }
                    break;
                }

                prev = curr;
                if (curr_is_clot) {
                    int index = LookupClotIndex(curr_as_clot, sem);
                    curr = curr_as_clot->nexts[index];
                } else {
                    curr = curr_as_thr->next_waiting_sem;
                }
            }
        }
    }
}

void CancelSems(struct thread* thr) {
    if (((struct obj_header*) thr->waiting_sem_or_clot)->objtype == OBJTYPE_WAITCLOT) {
        struct wait_clot* clot = thr->waiting_sem_or_clot;
        CleanupAfterSem(clot->sems, clot->count, thr);
        DerefObject(clot);
    } else {
        struct sem* sem = thr->waiting_sem_or_clot;
        CleanupAfterSem(&sem, 1, thr);
    }

    thr->waiting_sem_or_clot = NULL;
    thr->next_waiting_sem = NULL;
}

export int AcquireSemFromMany(struct sem** sems, int count, int64_t timeout, int* selected_out) {
    if (!IsSchedulingInitialised()) {
        return EINVAL;
    }
    
    AcquireScheduler();
    for (int i = 0; i < count; ++i) {
        if (sems[i]->count < sems[i]->max) {
            sems[i]->count++;
            ReleaseScheduler();
            if (selected_out != NULL) {
                *selected_out = i;
            }
            return 0;
        }
    }

    if (timeout == TIMEOUT_INSTANT) {
        ReleaseScheduler();
        return EAGAIN;
    }

    bool needs_timer_wait = timeout != TIMEOUT_INSTANT && timeout != TIMEOUT_INFINITE;
    if (needs_timer_wait) {
        // TODO:! 
    }

    struct thread* curr_thread = GetCurrentThread();
    AddThreadToSemQueues(curr_thread, sems, count);
    BlockThread();
    ReleaseScheduler();
    int retv = curr_thread->block_return_val;
    LogPrintf("AcquireSemFromMany: block retv was %d\n", retv);
    if (retv > 0) {
        return retv;
    }
    if (selected_out != NULL) { 
        *selected_out = -retv;
    }
    return 0;
}

export int AcquireSem(struct sem* sem, int64_t timeout) {
    if (!IsSchedulingInitialised()) {
        sem->count++;
        return 0;
    }
    return AcquireSemFromMany(&sem, 1, timeout, NULL);
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
            struct obj_header* hdr = sem->waiting_list_start;
            if (hdr->objtype == OBJTYPE_THREAD) {
                LogPrintf(":: Calling unblock thread with retv = 0\n");
                UnblockThread((struct thread*) sem->waiting_list_start, 0);
            } else {
                struct wait_clot* clot = sem->waiting_list_start;
                LogPrintf("Calling unblock thread with retv = %d\n", -LookupClotIndex(clot, sem));
                UnblockThread(clot->thr, -LookupClotIndex(clot, sem));
            }
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