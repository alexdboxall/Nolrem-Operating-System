#pragma once

struct sem;
struct mutex;

#include <common.h>
#include <obj.h>

struct thread;
struct wait_clot;

struct sem {
    struct obj_header hdr;
    int count;
    int max;
    void* waiting_list_start;
    void* waiting_list_end;
};

void KeInitSem(void);

struct sem* KeCreateSem(int max, int inital);
struct mutex* KeCreateMutex(void);

int KeAcquireSemFromMany(struct sem** sems, int count, int64_t timeout, int* selected_out);
int KeAcquireSem(struct sem* sem, int64_t timeout);
int KeAcquireMutex(struct mutex* mtx, int64_t timeout);
int KeReleaseSem(struct sem* sem);
int KeReleaseMutex(struct mutex* mtx);

void KeCancelSems(struct thread* thr);

void KeInitStaticSem(struct sem* sem, int max, int initial);
void KeDestroyStaticSem(struct sem* sem);

/* Should not generally be called on its own - is called as part of cleanup 
 * or KeDestroyStaticSem. */
void KeAbandonSem(struct sem* sem);