#include <common.h>
#include <thread.h>
#include <syscall.h>
#include <usersys.h>

#define OP_GET  0
#define OP_SET  1

pageable size_t SysLastError(size_t op, size_t value, size_t, size_t) {
    if (op == OP_GET) {
        return (size_t) KeGetCurrentThread()->last_error;
    }
    if (op == OP_SET) {
        KeSetLastUserError((int) value);
        return 0;
    }
    return EINVAL;
}

export pageableuserexec int GetLastError(void) {
    return (int) SystemCall3(SYS_LastError, OP_GET, 0);
}

export pageableuserexec void SetLastError(int error) {
    SystemCall3(SYS_LastError, OP_SET, (size_t) error);
}
