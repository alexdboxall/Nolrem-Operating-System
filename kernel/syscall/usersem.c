#include <common.h>
#include <obj.h>
#include <sem.h>
#include <syscall.h>
#include <usersync.h>
#include "usersync_internal.h"

pageable size_t SysCreateSemaphore(size_t max, size_t initial, size_t, size_t) {
    /* Written this way round so that a negative number from the user, which is a huge one here, is refused. */
    if (max < 1 || max > INT32_MAX || initial > max) {
        KeSetLastUserError(EINVAL);
        return 0;
    }
    if (KeIsOutOfMemory()) {
        KeSetLastUserError(ENOMEM);
        return 0;
    }

    /* The kernel's count is the number of units taken, where the user's is the number that are free. */
    struct sem* sem = KeCreateSem((int) max, (int) (max - initial));
    if (sem == NULL) {
        KeSetLastUserError(ENOMEM);
        return 0;
    }
    return (size_t) KeCreateUserRef(sem);       /* gives the semaphore its handle, taking our reference */
}

pageable size_t SysAcquireSemaphore(size_t handle, size_t timeout_lo, size_t timeout_hi, size_t) {
    struct sem* sem = KeValidateUserObjectAndAtomicallyRef((void*) handle, OBJ_SEM);
    if (sem == NULL) {
        KeSetLastUserError(EINVAL);
        return EINVAL;
    }

    /* We keep the reference that validating gave us for as long as we wait. So if the last handle is closed in
       the meantime the semaphore stays around until we are done with it, and is not freed from under us. */
    int64_t timeout = KeUserTimeoutToKernel(timeout_lo, timeout_hi);
    int res = KeAcquireSem(sem, timeout);
    KeDerefObject(sem);
    return KeFinishUserSync(res, timeout);
}

pageable size_t SysReleaseSemaphore(size_t handle, size_t, size_t, size_t) {
    struct sem* sem = KeValidateUserObjectAndAtomicallyRef((void*) handle, OBJ_SEM);
    if (sem == NULL) {
        KeSetLastUserError(EINVAL);
        return EINVAL;
    }
    int res = KeReleaseSemChecked(sem);
    KeDerefObject(sem);
    return KeFinishUserSyncNoWait(res);
}

export pageableuserexec semaphore_t CreateSemaphore(int max, int initial) {
    return (semaphore_t) SystemCall3(SYS_CreateSemaphore, (size_t) max, (size_t) initial);
}

export pageableuserexec int AcquireSemaphore(semaphore_t sem, uint64_t timeout_ns) {
    return SystemCall4(SYS_AcquireSemaphore, (size_t) sem, SYSCALL_U64_LO(timeout_ns), SYSCALL_U64_HI(timeout_ns));
}

export pageableuserexec int ReleaseSemaphore(semaphore_t sem) {
    return SystemCall2(SYS_ReleaseSemaphore, (size_t) sem);
}
