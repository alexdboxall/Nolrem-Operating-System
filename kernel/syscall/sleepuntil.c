#include <common.h>
#include <timer.h>
#include <syscall.h>
#include <usersys.h>

pageable size_t SysSleepUntil(size_t time_lo, size_t time_hi, size_t, size_t) {
    /* A time in the past (including one that is absurdly far ahead, but
     * that is a very long way off either way) is dealt with by the timer. */
    KeSleepUntil(SYSCALL_U64(time_lo, time_hi));
    return 0;
}

export pageableuserexec void SleepUntil(uint64_t time_since_boot_ns) {
    SystemCall3(SYS_SleepUntil, SYSCALL_U64_LO(time_since_boot_ns), SYSCALL_U64_HI(time_since_boot_ns));
}
