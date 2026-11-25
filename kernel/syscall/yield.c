#include <common.h>
#include <scheduler.h>
#include <syscall.h>
#include <usersys.h>

pageable size_t SysYield(size_t, size_t, size_t, size_t) {
    KeSchedule();
    return 0;
}

export pageableuserexec void Yield(void) {
    SystemCall1(SYS_Yield);
}
