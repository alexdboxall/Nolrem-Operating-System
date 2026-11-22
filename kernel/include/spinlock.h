#pragma once

#include <stddef.h>

struct spinlock {
    _Atomic char lock;
} __attribute__((packed));

void KeInitSpinlock(struct spinlock* lock);
void KeAcquireSpinlock(struct spinlock* lock);
void KeReleaseSpinlock(struct spinlock* lock);
bool KeTryAcquireSpinlock(struct spinlock* lock);

#ifdef COMPILE_KERNEL
/*
 * Code that is shared between the kernel and usermode (mem/heapex.c and
 * mem/allocvirtex.c) is written against the generic names, as usermode has its
 * own versions of these. In the kernel they resolve to the Ke versions, in the
 * same way that AllocHeap() does in common.h.
 */
#define InitSpinlock(lock) KeInitSpinlock(lock)
#define AcquireSpinlock(lock) KeAcquireSpinlock(lock)
#define ReleaseSpinlock(lock) KeReleaseSpinlock(lock)
#endif
