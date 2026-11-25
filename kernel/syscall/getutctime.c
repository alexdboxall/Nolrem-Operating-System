#include <common.h>
#include <arch.h>
#include <transfer.h>
#include <syscall.h>
#include <usersys.h>

pageable size_t SysGetUtcTime(size_t out_ptr, size_t, size_t, size_t) {
    /* There isn't a time zone setting for the kernel to give the RTC code, so
     * it is told that the clock is in UTC. */
    return KeWriteU64ToUsermode((uint64_t*) out_ptr, ArchGetUtcTime(0));
}

export pageableuserexec uint64_t GetUtcTime(void) {
    uint64_t value = 0;
    size_t res = SystemCall2(SYS_GetUtcTime, (size_t) &value);
    if (res != 0) {
        SetLastError((int) res);
        return 0;
    }
    return value;
}
