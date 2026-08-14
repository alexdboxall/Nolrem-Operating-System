#pragma once

struct sem;
struct mutex;

#define TIMEOUT_INFINITE (-1LL)
#define TIMEOUT_INSTANT  (0)

void InitSem(void);

struct sem* CreateSem(int max);
struct mutex* CreateMutex(void);

int AcquireSem(struct sem* sem, int64_t timeout);
int AcquireMutex(struct mutex* mtx, int64_t timeout);
int ReleaseSem(struct sem* sem);
int ReleaseMutex(struct mutex* mtx);