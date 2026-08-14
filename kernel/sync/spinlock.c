#include <spinlock.h>
#include <common.h>
#include <stdatomic.h>
#include <arch.h>

export userexec void InitSpinlock(struct spinlock* lock) {
    atomic_flag_clear(&lock->lock); 
}

export userexec void AcquireSpinlock(struct spinlock* lock) {
    while (atomic_flag_test_and_set_explicit(&lock->lock, memory_order_acquire)) {
        ArchIndicateSpinLoop();
    }
}

export userexec void ReleaseSpinlock(struct spinlock* lock) {
    atomic_flag_clear_explicit(&lock->lock, memory_order_release);
}