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

/*
 * Like KeReleaseSem() and KeReleaseMutex(), but for callers that can't be trusted to have acquired what they
 * are releasing (usermode). A release that nothing is holding is refused with EOVERFLOW, and changes nothing,
 * where the plain versions would let the count go below zero and so let in more holders than the maximum.
 * Sems and mutexes aren't owned by a thread, so this can't tell whether it is the caller's own acquire being
 * released, only that there is one to release.
 */
int KeReleaseSemChecked(struct sem* sem);
int KeReleaseMutexChecked(struct mutex* mtx);

/*
 * Releases two mutexes, in the order given, as one step: either both are held and both are released, or
 * one isn't and EOVERFLOW is returned with neither changed. Nothing can get in between the two.
 */
int KeReleaseMutexPairChecked(struct mutex* first, struct mutex* second);

void KeCancelSems(struct thread* thr);

void KeInitStaticSem(struct sem* sem, int max, int initial);
void KeDestroyStaticSem(struct sem* sem);

/* Should not generally be called on its own - is called as part of cleanup 
 * or KeDestroyStaticSem. */
void KeAbandonSem(struct sem* sem);