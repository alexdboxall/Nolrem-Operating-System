/*
 * Tests for the semaphore, mutex and reader/writer lock system calls (syscall/usersem.c, usermutex.c and
 * userrwlock.c). These call the handlers directly, as the system call dispatcher would, from a kernel thread.
 *
 * Only what can be done without blocking is here: every acquire has a timeout of 0, so one that can't be
 * satisfied fails straight away. The waiting itself is the kernel's, and is tested with the sems (test_sem_*).
 * (A mutex is a semaphore with room for one, so its calls are checked on their own below.)
 */

#include <_test.h>
#include <common.h>
#include <scheduler.h>
#include <thread.h>
#include <obj.h>
#include <sem.h>
#include <syscall.h>
#include <usersync.h>
#include <errno.h>

size_t SysCreateSemaphore(size_t, size_t, size_t, size_t);
size_t SysAcquireSemaphore(size_t, size_t, size_t, size_t);
size_t SysReleaseSemaphore(size_t, size_t, size_t, size_t);
size_t SysCreateMutex(size_t, size_t, size_t, size_t);
size_t SysAcquireMutex(size_t, size_t, size_t, size_t);
size_t SysReleaseMutex(size_t, size_t, size_t, size_t);
size_t SysCreateRwLock(size_t, size_t, size_t, size_t);
size_t SysAcquireRwLock(size_t, size_t, size_t, size_t);
size_t SysReleaseRwLock(size_t, size_t, size_t, size_t);
size_t SysRef(size_t, size_t, size_t, size_t);

#define READ        0
#define WRITE       1
#define NOWAIT      0, 0            /* the timeout, as its two halves */

static int LastErr(void) {
    return KeGetCurrentThread()->last_error;
}

/* Runs a call that is expected to fail, and returns the last error it set. */
#define FAILS_WITH(err, expr)                                       \
    do {                                                            \
        KeSetLastUserError(0);                                      \
        size_t _r = (expr);                                         \
        CHECK(_r == (size_t) (err));                                \
        CHECK(LastErr() == (err));                                  \
    } while (0)

#define SUCCEEDS(expr)                                              \
    do {                                                            \
        KeSetLastUserError(0);                                      \
        CHECK((expr) == 0);                                         \
        CHECK(LastErr() == 0);                                      \
    } while (0)

static void Close(size_t handle) {
    SysRef(handle, 1, 0, 0);
}

static void TestSemCounts(void) {
    /* 2 free of 3: two acquires work and the third doesn't. */
    size_t s = SysCreateSemaphore(3, 2, 0, 0);
    CHECK(s != 0);
    CHECK(GetObjectType((any_t) s) == OBJ_SEM);

    SUCCEEDS(SysAcquireSemaphore(s, NOWAIT, 0));
    SUCCEEDS(SysAcquireSemaphore(s, NOWAIT, 0));
    FAILS_WITH(EAGAIN, SysAcquireSemaphore(s, NOWAIT, 0));

    /*
     * Give them back. It is a semaphore, not a lock owned by whoever took it: the third unit (the one that was
     * not free to start with) can be released too, which makes all three free. Only a fourth, with nothing left
     * to give, is refused.
     */
    SUCCEEDS(SysReleaseSemaphore(s, 0, 0, 0));
    SUCCEEDS(SysReleaseSemaphore(s, 0, 0, 0));
    SUCCEEDS(SysReleaseSemaphore(s, 0, 0, 0));
    FAILS_WITH(EOVERFLOW, SysReleaseSemaphore(s, 0, 0, 0));
    FAILS_WITH(EOVERFLOW, SysReleaseSemaphore(s, 0, 0, 0));

    /* Refusing them left it as it was: all three free, so exactly three to take. */
    SUCCEEDS(SysAcquireSemaphore(s, NOWAIT, 0));
    SUCCEEDS(SysAcquireSemaphore(s, NOWAIT, 0));
    SUCCEEDS(SysAcquireSemaphore(s, NOWAIT, 0));
    FAILS_WITH(EAGAIN, SysAcquireSemaphore(s, NOWAIT, 0));
    Close(s);
}

static void TestSemCreateArgs(void) {
    KeSetLastUserError(0);
    CHECK(SysCreateSemaphore(0, 0, 0, 0) == 0);
    CHECK(LastErr() == EINVAL);

    KeSetLastUserError(0);
    CHECK(SysCreateSemaphore((size_t) -1, 0, 0, 0) == 0);
    CHECK(LastErr() == EINVAL);

    KeSetLastUserError(0);
    CHECK(SysCreateSemaphore(2, 3, 0, 0) == 0);                 /* more free than there can be */
    CHECK(LastErr() == EINVAL);

    KeSetLastUserError(0);
    CHECK(SysCreateSemaphore(2, (size_t) -1, 0, 0) == 0);       /* "negative" */
    CHECK(LastErr() == EINVAL);

    /* The edges that are fine. */
    size_t a = SysCreateSemaphore(1, 0, 0, 0);
    size_t b = SysCreateSemaphore(1, 1, 0, 0);
    CHECK(a != 0 && b != 0);
    FAILS_WITH(EAGAIN, SysAcquireSemaphore(a, NOWAIT, 0));
    SUCCEEDS(SysAcquireSemaphore(b, NOWAIT, 0));
    Close(a);
    Close(b);
}

static void TestMutex(void) {
    size_t m = SysCreateMutex(0, 0, 0, 0);
    CHECK(m != 0);

    FAILS_WITH(EOVERFLOW, SysReleaseMutex(m, 0, 0, 0));         /* it isn't held */
    SUCCEEDS(SysAcquireMutex(m, NOWAIT, 0));
    FAILS_WITH(EAGAIN, SysAcquireMutex(m, NOWAIT, 0));
    SUCCEEDS(SysReleaseMutex(m, 0, 0, 0));
    FAILS_WITH(EOVERFLOW, SysReleaseMutex(m, 0, 0, 0));
    SUCCEEDS(SysAcquireMutex(m, NOWAIT, 0));
    Close(m);
}

static void TestRwLock(void) {
    size_t rw = SysCreateRwLock(0, 0, 0, 0);
    CHECK(rw != 0);
    CHECK(GetObjectType((any_t) rw) == OBJ_RWLOCK);

    /* Nothing is held, so neither release is allowed. */
    FAILS_WITH(EOVERFLOW, SysReleaseRwLock(rw, READ, 0, 0));
    FAILS_WITH(EOVERFLOW, SysReleaseRwLock(rw, WRITE, 0, 0));

    /* Readers share, and a writer can't get in while there are any. */
    SUCCEEDS(SysAcquireRwLock(rw, READ, NOWAIT));
    SUCCEEDS(SysAcquireRwLock(rw, READ, NOWAIT));
    FAILS_WITH(EAGAIN, SysAcquireRwLock(rw, WRITE, NOWAIT));

    /* A reader is not a writer, so releasing the read hold as one is refused (and mustn't free the reader). */
    FAILS_WITH(EOVERFLOW, SysReleaseRwLock(rw, WRITE, 0, 0));
    SUCCEEDS(SysReleaseRwLock(rw, READ, 0, 0));
    FAILS_WITH(EAGAIN, SysAcquireRwLock(rw, WRITE, NOWAIT));
    SUCCEEDS(SysReleaseRwLock(rw, READ, 0, 0));
    FAILS_WITH(EOVERFLOW, SysReleaseRwLock(rw, READ, 0, 0));    /* one too many */

    /* A writer is alone. */
    SUCCEEDS(SysAcquireRwLock(rw, WRITE, NOWAIT));
    FAILS_WITH(EAGAIN, SysAcquireRwLock(rw, READ, NOWAIT));
    FAILS_WITH(EAGAIN, SysAcquireRwLock(rw, WRITE, NOWAIT));
    FAILS_WITH(EOVERFLOW, SysReleaseRwLock(rw, READ, 0, 0));    /* not a reader */
    SUCCEEDS(SysReleaseRwLock(rw, WRITE, 0, 0));
    FAILS_WITH(EOVERFLOW, SysReleaseRwLock(rw, WRITE, 0, 0));

    /* And after all that it is free, and works the same again. */
    SUCCEEDS(SysAcquireRwLock(rw, WRITE, NOWAIT));
    SUCCEEDS(SysReleaseRwLock(rw, WRITE, 0, 0));
    SUCCEEDS(SysAcquireRwLock(rw, READ, NOWAIT));
    SUCCEEDS(SysReleaseRwLock(rw, READ, 0, 0));

    /* Not a mode. */
    FAILS_WITH(EINVAL, SysAcquireRwLock(rw, 2, NOWAIT));
    FAILS_WITH(EINVAL, SysReleaseRwLock(rw, 2, 0, 0));
    Close(rw);
}

static void TestBadHandles(void) {
    size_t sem = SysCreateSemaphore(1, 1, 0, 0);
    size_t rw = SysCreateRwLock(0, 0, 0, 0);
    CHECK(sem != 0 && rw != 0);

    /* Nothing, rubbish that isn't a handle at all, and the wrong sort of handle. */
    FAILS_WITH(EINVAL, SysAcquireSemaphore(0, NOWAIT, 0));
    FAILS_WITH(EINVAL, SysReleaseSemaphore(0, 0, 0, 0));
    FAILS_WITH(EINVAL, SysAcquireMutex(0, NOWAIT, 0));
    FAILS_WITH(EINVAL, SysReleaseMutex(0, 0, 0, 0));
    FAILS_WITH(EINVAL, SysAcquireRwLock(0, READ, NOWAIT));
    FAILS_WITH(EINVAL, SysReleaseRwLock(0, READ, 0, 0));

    FAILS_WITH(EINVAL, SysAcquireSemaphore(rw, NOWAIT, 0));
    FAILS_WITH(EINVAL, SysReleaseSemaphore(rw, 0, 0, 0));
    FAILS_WITH(EINVAL, SysAcquireMutex(rw, NOWAIT, 0));
    FAILS_WITH(EINVAL, SysReleaseMutex(rw, 0, 0, 0));
    FAILS_WITH(EINVAL, SysAcquireRwLock(sem, READ, NOWAIT));
    FAILS_WITH(EINVAL, SysReleaseRwLock(sem, WRITE, 0, 0));

    /* None of that touched either of them. */
    SUCCEEDS(SysAcquireSemaphore(sem, NOWAIT, 0));
    SUCCEEDS(SysAcquireRwLock(rw, WRITE, NOWAIT));
    SUCCEEDS(SysReleaseRwLock(rw, WRITE, 0, 0));

    /* A closed one: Ref() it so we can close it and still have the handle, then close the rest of the way. */
    SysRef(sem, 0, 0, 0);
    Close(sem);
    SUCCEEDS(SysReleaseSemaphore(sem, 0, 0, 0));               /* one handle left, so still good */
    Close(sem);
    Close(rw);
}

export void RunUserSyncTests(void) {
    RUN_TEST(TestSemCounts);
    RUN_TEST(TestSemCreateArgs);
    RUN_TEST(TestMutex);
    RUN_TEST(TestRwLock);
    RUN_TEST(TestBadHandles);

    TRACE("user sync tests: ALL PASSED\n");
}
