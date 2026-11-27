#include <common.h>
#include <obj.h>
#include <mutex.h>
#include <syscall.h>
#include <usersync.h>
#include "usersync_internal.h"

/*
 * Usermode has separate calls and types for mutexes and semaphores, but to the kernel a mutex is a semaphore
 * that allows one holder at a time (that is all struct mutex is), so that is what is made and what is checked
 * for below. Nothing here relies on it being one made by CreateMutex().
 */
pageable size_t SysCreateMutex(size_t, size_t, size_t, size_t) {
    if (KeIsOutOfMemory()) {
        KeSetLastUserError(ENOMEM);
        return 0;
    }
    struct mutex* mtx = KeCreateMutex();
    if (mtx == NULL) {
        KeSetLastUserError(ENOMEM);
        return 0;
    }
    return (size_t) KeCreateUserRef(mtx);       /* gives the mutex its handle, taking our reference */
}

pageable size_t SysAcquireMutex(size_t handle, size_t timeout_lo, size_t timeout_hi, size_t) {
    struct mutex* mtx = KeValidateUserObjectAndAtomicallyRef((void*) handle, OBJ_SEM);
    if (mtx == NULL) {
        KeSetLastUserError(EINVAL);
        return EINVAL;
    }

    /* As for semaphores: the reference is kept for as long as we wait. */
    int64_t timeout = KeUserTimeoutToKernel(timeout_lo, timeout_hi);
    int res = KeAcquireMutex(mtx, timeout);
    KeDerefObject(mtx);
    return KeFinishUserSync(res, timeout);
}

pageable size_t SysReleaseMutex(size_t handle, size_t, size_t, size_t) {
    struct mutex* mtx = KeValidateUserObjectAndAtomicallyRef((void*) handle, OBJ_SEM);
    if (mtx == NULL) {
        KeSetLastUserError(EINVAL);
        return EINVAL;
    }
    int res = KeReleaseMutexChecked(mtx);
    KeDerefObject(mtx);
    return KeFinishUserSyncNoWait(res);
}

export pageableuserexec mutex_t CreateMutex(void) {
    return (mutex_t) SystemCall1(SYS_CreateMutex);
}

export pageableuserexec int AcquireMutex(mutex_t mtx, uint64_t timeout_ns) {
    return SystemCall4(SYS_AcquireMutex, (size_t) mtx, SYSCALL_U64_LO(timeout_ns), SYSCALL_U64_HI(timeout_ns));
}

export pageableuserexec int ReleaseMutex(mutex_t mtx) {
    return SystemCall2(SYS_ReleaseMutex, (size_t) mtx);
}
