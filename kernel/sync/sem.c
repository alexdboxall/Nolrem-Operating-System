#include <obj.h>
#include <heap.h>
#include <common.h>
#include <sem.h>
#include <errno.h>
#include <thread.h>
#include <scheduler.h>
#include <log.h>
#include <string.h>
#include <timer.h>
#include <mutex.h>

struct wait_clot {
    struct obj_header hdr;
    struct thread* thr;
    int count;
    struct sem** sems;
    void** nexts;
};

static void AbandonSemLocked(struct sem* sem) {
    while (sem->waiting_list_start != NULL) {
        void* head = sem->waiting_list_start;
        struct thread* thr;

        if (KeGetObjectType((struct obj_header*) head) == OBJTYPE_WAITCLOT) {
            thr = ((struct wait_clot*) head)->thr;
        } else {
            thr = head;
        }

        KeUnblockThread(thr, EABANDONED);
        if (sem->waiting_list_start == head) {
            KeCancelSems(thr);
        }
    }
}

void KeAbandonSem(struct sem* sem) {
    KeAcquireScheduler();
    AbandonSemLocked(sem);
    KeReleaseScheduler();
}

static void CleanupSem(void* _sem) {
    struct sem* sem = _sem;
    KeAbandonSem(sem);
    FreeHeap(sem);
}

static void CleanupWaitClot(void* _clot) {
    struct wait_clot* clot = _clot;
    FreeHeap(clot->sems);
    FreeHeap(clot->nexts);
    FreeHeap(clot);
}

void KeDestroyStaticSem(struct sem* sem) {
    KeAbandonSem(sem);
}

void KeInitSem(void) {
    KeRegisterObjectType(OBJTYPE_SEM, CleanupSem);
    KeRegisterObjectType(OBJTYPE_WAITCLOT, CleanupWaitClot);
}

export void KeInitStaticSem(struct sem* sem, int max, int initial) {
    KeInitObject(sem, OBJTYPE_SEM);
    sem->count = initial;
    sem->max = max;
    sem->waiting_list_start = NULL;
    sem->waiting_list_end = NULL;
}

export struct sem* KeCreateSem(int max, int initial) {
    struct sem* sem = AllocHeap(sizeof(struct sem));
    if (sem == NULL) {
        return NULL;
    }
    KeInitStaticSem(sem, max, initial);
    return sem;
}

export struct mutex* KeCreateMutex(void) {
    return (struct mutex*) KeCreateSem(1, 0);
}

static struct wait_clot* CreateClot(struct sem** sems, int count, struct thread* thr) {
    struct wait_clot* clot = AllocHeap(sizeof(struct wait_clot));
    if (clot == NULL) {
        return NULL;
    }
    KeInitObject(clot, OBJTYPE_WAITCLOT);
    clot->count = count;
    clot->thr = thr;
    clot->sems = AllocHeap(sizeof(struct sem*) * count);
    if (clot->sems == NULL) {
        FreeHeap(clot);
        return NULL;
    }
    KeLogPrintf("A..?\n");
    clot->nexts = AllocHeap(sizeof(void*) * count);
    if (clot->nexts == NULL) {
        FreeHeap(clot->sems);
        FreeHeap(clot);
        return NULL;
    }
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

static int AddThreadToSemQueues(struct thread* thr, struct sem** sems, int count) {
    void* to_add;
    if (count > 1) {
        to_add = CreateClot(sems, count, thr);
        if (to_add == NULL) {
            return ENOMEM;
        }
    } else {
        to_add = thr;
    }

    thr->next_waiting_sem = NULL;
    thr->waiting_sem_or_clot = count > 1 ? to_add : sems[0];

    for (int i = 0; i < count; ++i) {
        struct sem* sem = sems[i];

        /* Point the existing last thing to the newly added last thing. */
        if (sem->waiting_list_end != NULL) {
            if (KeGetObjectType(sem->waiting_list_end) == OBJTYPE_THREAD) {
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
    return 0;
}

static void CleanupAfterSem(struct sem** sems, int count, struct thread* thr) {
    for (int i = 0; i < count; ++i) {
        struct sem* sem = sems[i];

        uint8_t head_hdr_type = KeGetObjectType(sem->waiting_list_start);
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
                bool curr_is_clot = KeGetObjectType(curr) == OBJTYPE_WAITCLOT;
                struct wait_clot* prev_as_clot = prev;
                struct thread* prev_as_thr = prev;
                bool prev_is_clot = KeGetObjectType(prev) == OBJTYPE_WAITCLOT;
                int curr_clot_idx = curr_is_clot ? LookupClotIndex(curr_as_clot, sem) : -1;
                int prev_clot_idx = prev_is_clot ? LookupClotIndex(prev_as_clot, sem) : -1;

                if ((!curr_is_clot && curr_as_thr == thr) || (curr_is_clot && curr_as_clot->thr == thr)) {
                    void* rhs = curr_is_clot ? curr_as_clot->nexts[curr_clot_idx] : curr_as_thr->next_waiting_sem;
                    void** lhs = prev_is_clot 
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

void KeCancelSems(struct thread* thr) {
    if (KeGetObjectType(thr->waiting_sem_or_clot) == OBJTYPE_WAITCLOT) {
        struct wait_clot* clot = thr->waiting_sem_or_clot;
        CleanupAfterSem(clot->sems, clot->count, thr);
        KeDerefObject(clot);
    } else {
        struct sem* sem = thr->waiting_sem_or_clot;
        CleanupAfterSem(&sem, 1, thr);
    }

    thr->waiting_sem_or_clot = NULL;
    thr->next_waiting_sem = NULL;
}

export int KeAcquireSemFromMany(struct sem** sems, int count, int64_t timeout, int* selected_out) {
    if (!KeIsSchedulingInitialised()) {
        return EINVAL;
    }
    
    KeAcquireScheduler();
    for (int i = 0; i < count; ++i) {
        if (sems[i]->count < sems[i]->max) {
            sems[i]->count++;
            KeReleaseScheduler();
            if (selected_out != NULL) {
                *selected_out = i;
            }
            return 0;
        }
    }

    if (timeout == TIMEOUT_INSTANT) {
        KeReleaseScheduler();
        return EAGAIN;
    }

    bool needs_timer_wait = timeout != TIMEOUT_INSTANT && timeout != TIMEOUT_INFINITE;
    if (needs_timer_wait) {
        bool added = KeAddThreadToSleepQueue(true, timeout, false);
        if (!added) {
            /* i.e. already in the past */
            KeReleaseScheduler();
            return EAGAIN;
        }
    }

    struct thread* curr_thread = KeGetCurrentThread();
    int res = AddThreadToSemQueues(curr_thread, sems, count);
    if (res != 0) {
        KeReleaseScheduler();
        return res;
    }
    KeBlockThread();
    KeReleaseScheduler();
    int retv = curr_thread->block_return_val;
    if (retv > 0) {
        return retv;
    }
    if (selected_out != NULL) { 
        *selected_out = -retv;
    }
    return 0;
}

export int KeAcquireSem(struct sem* sem, int64_t timeout) {
    if (!KeIsSchedulingInitialised()) {
        sem->count++;
        return 0;
    }
    return KeAcquireSemFromMany(&sem, 1, timeout, NULL);
}

export int KeReleaseSem(struct sem* sem) {
    if (!KeIsSchedulingInitialised()) {
        sem->count--;
        return 0;
    }

    KeAcquireScheduler();

    if (sem->count == sem->max) {
        if (sem->waiting_list_start == NULL) {
            sem->count--;
        } else {
            struct obj_header* hdr = sem->waiting_list_start;
            if (KeGetObjectType(hdr) == OBJTYPE_THREAD) {
                KeUnblockThread((struct thread*) sem->waiting_list_start, 0);
            } else {
                struct wait_clot* clot = sem->waiting_list_start;
                KeUnblockThread(clot->thr, -LookupClotIndex(clot, sem));
            }
        }

    } else {
        sem->count--;
    }
    KeReleaseScheduler();
    return 0;
}

export void KeInitStaticMutex(struct mutex* mtx) {
    KeInitStaticSem(&mtx->sem, 1, 0);
}

export void KeDestroyStaticMutex(struct mutex* mtx) {
    KeDestroyStaticSem(&mtx->sem);
}

export int KeAcquireMutex(struct mutex* mtx, int64_t timeout) {
    return KeAcquireSem((struct sem*) mtx, timeout);
}

export int KeReleaseMutex(struct mutex* mtx) {
    return KeReleaseSem((struct sem*) mtx);
}
