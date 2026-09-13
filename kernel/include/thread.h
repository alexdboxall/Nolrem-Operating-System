#pragma once

#include <obj.h>

struct sem;

#define THREAD_STATE_RUNNING    0
#define THREAD_STATE_READY      1
#define THREAD_STATE_BLOCKED    2

#define PRIORITY_MAX            0
#define PRIORITY_HIGH           10
#define PRIORITY_NORMAL         128
#define PRIORITY_LOW            240
#define PRIORITY_IDLE           255

struct thread {
    struct obj_header hdr;

    /* This data starts at offset 8 into the `struct thread`. 
     * Platform specific data relies on this. */
    size_t kernel_stack_top __attribute__ ((aligned(8)));
    size_t stack_pointer;
    struct vas* vas;
    size_t kernel_stack_size;
    size_t user_stack_base;

    uint8_t state;
    uint8_t priority;
    int block_return_val;

    struct thread* next_ready;
    struct thread* next_waiting_sem;
    struct thread* next_waiting_timer;
    void* waiting_sem_or_clot;
};

struct thread* CreateThread(struct vas* vas, void(*entry)(void*), void* context);
uint8_t GetThreadPriority(struct thread* thr);
void SetThreadPriority(struct thread* thr, uint8_t priority);

struct thread* GetCurrentThread(void);
void BlockThread(void);
void UnblockThread(struct thread* thr, int retv);

void InitThread();