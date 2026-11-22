#pragma once

#include <common.h>
#include <spinlock.h>

struct virt_range;

struct virt_arena {
    struct virt_range* free_list_head;
    struct spinlock lock;
};

void InitVirtArena(struct virt_arena* va, size_t start, size_t bytes);
void KeInitKernelVirtArena(void);
struct virt_arena* KeGetKernelVirtArena(void);

size_t AllocVirtEx(struct virt_arena* va, size_t bytes);
size_t KeAllocVirt(size_t bytes);

void FreeVirtEx(struct virt_arena* va, size_t start_addr, size_t bytes);
void KeFreeVirt(size_t start_addr, size_t bytes);