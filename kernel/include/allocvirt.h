#pragma once

#include <common.h>
#include <spinlock.h>

struct virt_range;

struct virt_arena {
    struct virt_range* free_list_head;
    struct spinlock lock;
};

void InitVirtArena(struct virt_arena* va, size_t start, size_t bytes);
void InitKernelVirtArena(void);
struct virt_arena* GetKernelVirtArena(void);

size_t AllocVirtEx(struct virt_arena* va, size_t bytes);
size_t AllocVirt(size_t bytes);

void FreeVirtEx(struct virt_arena* va, size_t start_addr, size_t bytes);
void FreeVirt(size_t start_addr, size_t bytes);