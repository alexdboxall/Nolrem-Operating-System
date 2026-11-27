#include <common.h>
#include <obj.h>
#include <rwlock.h>
#include <syscall.h>
#include <usersync.h>
#include "usersync_internal.h"

#define RW_READ     0
#define RW_WRITE    1

pageable size_t SysCreateRwLock(size_t, size_t, size_t, size_t) {
    if (KeIsOutOfMemory()) {
        KeSetLastUserError(ENOMEM);
        return 0;
    }
    struct rwlock* rw = KeCreateRwLock();
    if (rw == NULL) {
        KeSetLastUserError(ENOMEM);
        return 0;
    }
    return (size_t) KeCreateUserRef(rw);        /* gives the lock its handle, taking our reference */
}

pageable size_t SysAcquireRwLock(size_t handle, size_t mode, size_t timeout_lo, size_t timeout_hi) {
    if (mode != RW_READ && mode != RW_WRITE) {
        KeSetLastUserError(EINVAL);
        return EINVAL;
    }
    struct rwlock* rw = KeValidateUserObjectAndAtomicallyRef((void*) handle, OBJ_RWLOCK);
    if (rw == NULL) {
        KeSetLastUserError(EINVAL);
        return EINVAL;
    }

    /* As for semaphores: the reference is kept for as long as we wait. */
    int64_t timeout = KeUserTimeoutToKernel(timeout_lo, timeout_hi);
    int res = mode == RW_READ ? KeAcquireRwLockRead(rw, timeout) : KeAcquireRwLockWrite(rw, timeout);
    KeDerefObject(rw);
    return KeFinishUserSync(res, timeout);
}

pageable size_t SysReleaseRwLock(size_t handle, size_t mode, size_t, size_t) {
    if (mode != RW_READ && mode != RW_WRITE) {
        KeSetLastUserError(EINVAL);
        return EINVAL;
    }
    struct rwlock* rw = KeValidateUserObjectAndAtomicallyRef((void*) handle, OBJ_RWLOCK);
    if (rw == NULL) {
        KeSetLastUserError(EINVAL);
        return EINVAL;
    }
    int res = mode == RW_READ ? KeReleaseRwLockReadChecked(rw) : KeReleaseRwLockWriteChecked(rw);
    KeDerefObject(rw);
    return KeFinishUserSyncNoWait(res);
}

export pageableuserexec rwlock_t CreateRwLock(void) {
    return (rwlock_t) SystemCall1(SYS_CreateRwLock);
}

export pageableuserexec int AcquireRwLockRead(rwlock_t rw, uint64_t timeout_ns) {
    return SystemCall(SYS_AcquireRwLock, (size_t) rw, RW_READ, SYSCALL_U64_LO(timeout_ns), SYSCALL_U64_HI(timeout_ns));
}

export pageableuserexec int AcquireRwLockWrite(rwlock_t rw, uint64_t timeout_ns) {
    return SystemCall(SYS_AcquireRwLock, (size_t) rw, RW_WRITE, SYSCALL_U64_LO(timeout_ns), SYSCALL_U64_HI(timeout_ns));
}

export pageableuserexec int ReleaseRwLockRead(rwlock_t rw) {
    return SystemCall3(SYS_ReleaseRwLock, (size_t) rw, RW_READ);
}

export pageableuserexec int ReleaseRwLockWrite(rwlock_t rw) {
    return SystemCall3(SYS_ReleaseRwLock, (size_t) rw, RW_WRITE);
}
