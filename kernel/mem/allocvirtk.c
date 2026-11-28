#include <common.h>
#include <spinlock.h>
#include <arch.h>
#include <allocvirt.h>
#include <heap.h>

static struct virt_arena krnl_va;

void KeInitKernelVirtArena(void) {
    InitVirtArena(&krnl_va, ARCH_KRNL_VIRT_RANGE_BASE, ARCH_KRNL_VIRT_RANGE_BYTES);
}

export size_t KeAllocVirt(size_t bytes) {
    return AllocVirtEx(&krnl_va, bytes);
}

export size_t KeAllocVirtNoHeap(size_t bytes) {
    return AllocVirtExNoHeap(&krnl_va, bytes);
}

export void KeFreeVirt(size_t start, size_t bytes) {
    FreeVirtEx(&krnl_va, start, bytes);
}

struct virt_arena* KeGetKernelVirtArena(void) {
    return &krnl_va;
}