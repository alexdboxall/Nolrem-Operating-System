#pragma once

#include <common.h>
#include <syscall.h>
#include <usersync.h>
#include <errno.h>

/*
 * What the system calls for semaphores, mutexes and reader/writer locks have in common. Only for the files in
 * this directory.
 */

/*
 * The longest we will wait for: 2^62ns, which is about 146 years. The timer adds the wait to the time since
 * boot, and a time that wrapped round would come out as being in the past, and so not wait at all. Anything
 * longer is cut down to this (it will not make a difference to anyone), and UINT64_MAX means for ever.
 */
#define USER_MAX_WAIT_NS    (1ULL << 62)

/* Turns the two halves of the user's timeout into the kernel's. */
static inline int64_t KeUserTimeoutToKernel(size_t lo, size_t hi) {
    uint64_t ns = SYSCALL_U64(lo, hi);
    if (ns == UINT64_MAX) {
        return TIMEOUT_INFINITE;
    }
    if (ns == 0) {
        return TIMEOUT_INSTANT;
    }
    return (int64_t) (ns > USER_MAX_WAIT_NS ? USER_MAX_WAIT_NS : ns);
}

/*
 * Reports the result of a kernel call as the system call's: 0, or an errno that is also set as the last error.
 * A wait that has a time that has run out fails with ETIMEDOUT, but the kernel calls one that was already too
 * late to start, because the time was so short, EAGAIN, the same as if it had been told not to wait at all. Only
 * a timeout of 0 gets to see that.
 */
static inline size_t KeFinishUserSync(int res, int64_t timeout) {
    if (res == 0) {
        return 0;
    }
    if (res == EAGAIN && timeout != TIMEOUT_INSTANT) {
        res = ETIMEDOUT;
    }
    KeSetLastUserError(res);
    return (size_t) res;
}

/* The same, for the calls that have no timeout. */
static inline size_t KeFinishUserSyncNoWait(int res) {
    return KeFinishUserSync(res, TIMEOUT_INSTANT);
}
