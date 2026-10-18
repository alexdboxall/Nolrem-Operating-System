#pragma once

#include <stddef.h>

struct spinlock {
    _Atomic char lock;
} __attribute__((packed));

void InitSpinlock(struct spinlock* lock);
void AcquireSpinlock(struct spinlock* lock);
void ReleaseSpinlock(struct spinlock* lock);
bool TryAcquireSpinlock(struct spinlock* lock);