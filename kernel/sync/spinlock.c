#include <spinlock.h>
#include <common.h>
#include <interrupt.h>
#include <stdatomic.h>
#include <arch.h>

export void KeInitSpinlock(struct spinlock* lock) {
    atomic_flag_clear(&lock->lock); 
}

export void KeAcquireSpinlock(struct spinlock* lock) {
    KeAcquireIrqPrevent();
    while (atomic_flag_test_and_set_explicit(&lock->lock, memory_order_acquire)) {
        ArchIndicateSpinLoop();
    }
}

export inline __attribute__((__always_inline__)) void KeReleaseSpinlock(struct spinlock* lock) {
    atomic_flag_clear_explicit(&lock->lock, memory_order_release);
    KeReleaseIrqPrevent();
}

export inline __attribute__((__always_inline__)) bool KeTryAcquireSpinlock(struct spinlock* lock) {
    KeAcquireIrqPrevent();
    bool acquired = !atomic_flag_test_and_set_explicit(&lock->lock, memory_order_acquire);
    if (!acquired) {
        KeReleaseIrqPrevent();
    }
    return acquired;
}