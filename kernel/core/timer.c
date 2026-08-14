#include <timer.h>
#include <common.h>
#include <spinlock.h>

struct spinlock timer_spinlock;
static uint64_t ns_since_boot;

uint64_t GetTimeSinceBoot(void) {
    AcquireSpinlock(&timer_spinlock);
    uint64_t retv = ns_since_boot;
    ReleaseSpinlock(&timer_spinlock);
    return retv;
}

void AdvanceTimer(uint64_t ns) {
    AcquireSpinlock(&timer_spinlock);
    ns_since_boot += ns;
    ReleaseSpinlock(&timer_spinlock);
}

void InitTimer(void) {
    InitSpinlock(&timer_spinlock);
}