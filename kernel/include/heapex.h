#pragma once

#include <stddef.h>
#include <spinlock.h>
#include <common.h>

#define TOTAL_NUM_FREE_LISTS 16

struct block;

struct heap {
    /**
     * Global arrays always initialise to zero (and therefore, to NULL).
     * Entries in free lists must have a user allocated size GREATER OR EQUAL TO the
     * size in free_list_block_sizes.
     */
    struct block* _head_block[TOTAL_NUM_FREE_LISTS];

    void*(*get_memory)(size_t*);
    struct spinlock lock;
};


void InitHeapEx(struct heap*, void*(*get_memory)(size_t*));

void* AllocHeapEx(struct heap* heap, size_t bytes);
void FreeHeapEx(struct heap* heap, void* ptr);
void* ReallocHeapEx(struct heap* heap, void* ptr, size_t new_size);
size_t GetAllocationSizeEx(struct heap* heap, void* ptr);
