#include <common.h>
#include <spinlock.h>
#include <arch.h>
#include <allocvirt.h>
#include <heap.h>

static struct virt_arena krnl_va;

void InitKernelVirtArena(void) {
    InitVirtArena(&krnl_va, ARCH_KRNL_VIRT_RANGE_BASE, ARCH_KRNL_VIRT_RANGE_BYTES);
}

export size_t AllocVirt(size_t bytes) {
    return AllocVirtEx(&krnl_va, bytes);
}

export void FreeVirt(size_t start, size_t bytes) {
    FreeVirtEx(&krnl_va, start, bytes);
}