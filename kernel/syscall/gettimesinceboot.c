#include <common.h>
#include <timer.h>
#include <transfer.h>
#include <syscall.h>
#include <usersys.h>

pageable size_t SysGetTimeSinceBoot(size_t out_ptr, size_t, size_t, size_t) {
    return KeWriteU64ToUsermode((uint64_t*) out_ptr, KeGetTimeSinceBoot());
}

export pageableuserexec uint64_t GetTimeSinceBoot(void) {
    uint64_t value = 0;
    size_t res = SystemCall2(SYS_GetTimeSinceBoot, (size_t) &value);
    if (res != 0) {
        SetLastError((int) res);
        return 0;
    }
    return value;
}
