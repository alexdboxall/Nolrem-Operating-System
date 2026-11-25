#include <common.h>
#include <timer.h>
#include <scheduler.h>
#include <syscall.h>
#include <usersys.h>

/*
 * Longest we will sleep for: 2^62ns, which is about 146 years. The timer adds
 * the sleep to the time since boot, and a time that wrapped round would come out
 * as being in the past, and so not sleep at all.
 */
#define MAX_SLEEP_NS    (1ULL << 62)

pageable size_t SysSleep(size_t ns_lo, size_t ns_hi, size_t, size_t) {
    uint64_t ns = SYSCALL_U64(ns_lo, ns_hi);
    if (ns == 0) {
        /* The timer says that nothing needs to be waited for, so wouldn't yield. */
        KeSchedule();
        return 0;
    }
    if (ns > MAX_SLEEP_NS) {
        ns = MAX_SLEEP_NS;
    }
    KeSleepNano(ns);
    return 0;
}

export pageableuserexec void SleepNano(uint64_t ns) {
    SystemCall3(SYS_Sleep, SYSCALL_U64_LO(ns), SYSCALL_U64_HI(ns));
}

export pageableuserexec void SleepMilli(uint64_t ms) {
    /* Saturates rather than wrapping round into a short sleep. */
    if (ms > UINT64_MAX / 1000000) {
        SleepNano(UINT64_MAX);
    } else {
        SleepNano(ms * 1000000);
    }
}
