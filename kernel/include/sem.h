#pragma once

struct sem;
struct mutex;

#include <obj.h>

struct thread;

#define TIMEOUT_INFINITE (-1LL)
#define TIMEOUT_INSTANT  (0)

struct sem {
    struct obj_header hdr;
    int count;
    int max;
    struct thread* waiting_list_start;
    struct thread* waiting_list_end;
};

void InitSem(void);

struct sem* CreateSem(int max, int inital);
struct mutex* CreateMutex(void);

int AcquireSem(struct sem* sem, int64_t timeout);
int AcquireMutex(struct mutex* mtx, int64_t timeout);
int ReleaseSem(struct sem* sem);
int ReleaseMutex(struct mutex* mtx);

void InitStaticSem(struct sem* sem, int max, int initial);
void DestroyStaticSem(struct sem* sem);