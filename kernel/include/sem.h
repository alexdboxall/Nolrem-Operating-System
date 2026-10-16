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

void InitSem(void);

struct sem* CreateSem(int max, int inital);
struct mutex* CreateMutex(void);

int AcquireSemFromMany(struct sem** sems, int count, int64_t timeout, int* selected_out);
int AcquireSem(struct sem* sem, int64_t timeout);
int AcquireMutex(struct mutex* mtx, int64_t timeout);
int ReleaseSem(struct sem* sem);
int ReleaseMutex(struct mutex* mtx);

void CancelSems(struct thread* thr);

void InitStaticSem(struct sem* sem, int max, int initial);
void DestroyStaticSem(struct sem* sem);

/* Should not generally be called on its own - is called as part of cleanup 
 * or DestroyStaticSem. */
void AbandonSem(struct sem* sem);