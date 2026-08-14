#pragma once

struct sem;

#define THREAD_STATE_RUNNING    0
#define THREAD_STATE_READY      1
#define THREAD_STATE_BLOCKED    2

struct thread {
    size_t kernel_stack_top;
    size_t stack_pointer;
    struct vas* vas;
    size_t kernel_stack_size;

    int state;
    int block_return_val;

    struct thread* next_waiting_sem;
    struct thread* next_waiting_timer;
    struct sem* waiting_sem;
};

struct thread* GetCurrentThread(void);
void SetThreadWaitingSem(struct thread* thr, struct sem* sem);
void BlockThread(void);
void UnblockThread(struct thread* thr, int retv);

void InitThread();