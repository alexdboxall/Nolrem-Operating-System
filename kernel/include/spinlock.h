#pragma once

#include <stddef.h>

struct spinlock {
    _Atomic char lock;
};

void InitSpinlock(struct spinlock* lock);
void AcquireSpinlock(struct spinlock* lock);
void ReleaseSpinlock(struct spinlock* lock);