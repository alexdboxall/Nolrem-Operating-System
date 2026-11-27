#pragma once

#include <common.h>
#include <obj.h>
#include <mutex.h>

/*
 * A sleeping reader/writer lock: any number of threads can hold it for reading
 * at once, or exactly one can hold it for writing.
 *
 * It is built entirely out of three mutexes, in the same way a mutex is a
 * special case of a semaphore. They are embedded in the structure and set up
 * as static mutexes, so a rwlock is one allocation (or none, for a static
 * one) and can be initialised before the heap is up.
 *
 * Behaviour worth knowing about:
 *
 *  - Fair, with writers not starved: a writer that is waiting holds back any
 *    reader that arrives after it, so a steady stream of readers can't keep a
 *    writer out. The mutexes wake their waiters first-come first-served, and
 *    that order is what the lock hands out.
 *  - Not recursive, and no upgrading. A thread that holds the lock for reading
 *    and then asks for it again (for reading or writing) can deadlock behind a
 *    writer that arrived in between. Take the stronger lock first.
 *  - Neither mode is tied to a thread, so a lock may be released by a different
 *    thread from the one that took it (like the mutexes underneath).
 *  - It sleeps, so it can't be used from an interrupt handler, or anywhere a
 *    spinlock is held. Use a spinlock for those.
 *  - Timeouts work as they do for KeAcquireMutex: TIMEOUT_INSTANT is a try-lock,
 *    TIMEOUT_INFINITE waits forever, and anything else is a relative time in
 *    nanoseconds covering the whole acquire. A failed acquire leaves the lock
 *    exactly as it was.
 *
 * As with static mutexes, a static rwlock must be torn down with
 * KeDestroyStaticRwLock(), never KeDerefObject().
 */
struct rwlock {
    struct obj_header hdr;      /* Must go first, like every other object. */

    /*
     * The turnstile. A writer holds it from the moment it starts waiting until
     * it is finished; a reader just passes through it (take it, drop it). So
     * readers arriving behind a waiting writer queue up here instead of
     * slipping in ahead of it.
     */
    struct mutex gate;

    /*
     * Held by the writer, or by the readers as a group (taken by the first one
     * in, dropped by the last one out - which is why a mutex that doesn't care
     * about its owner is needed).
     */
    struct mutex room;

    /* Protects 'readers'. */
    struct mutex readers_lock;
    int readers;
};

/* Registers the object type. Called once, during kernel startup. */
void KeInitRwLock(void);

struct rwlock* KeCreateRwLock(void);

void KeInitStaticRwLock(struct rwlock* rw);
void KeDestroyStaticRwLock(struct rwlock* rw);

/* Return 0 on success, or an errno (EAGAIN/ETIMEDOUT, EABANDONED, ...) with nothing held. */
int KeAcquireRwLockRead(struct rwlock* rw, int64_t timeout);
int KeAcquireRwLockWrite(struct rwlock* rw, int64_t timeout);

int KeReleaseRwLockRead(struct rwlock* rw);
int KeReleaseRwLockWrite(struct rwlock* rw);

/*
 * The same, for callers that can't be trusted to hold what they are releasing (usermode). The plain versions
 * assert, or let the lock's counts go below zero; these refuse with EOVERFLOW instead, and change nothing.
 *
 * That is all they can do. The lock doesn't know who holds it, so a release that nothing could have been
 * holding is caught, but a thread releasing a hold that is really another thread's is not, and it takes the
 * exclusion away from them. That is the caller's own bug and spoils only this lock: nothing in the kernel
 * takes a lock that usermode can reach.
 */
int KeReleaseRwLockReadChecked(struct rwlock* rw);
int KeReleaseRwLockWriteChecked(struct rwlock* rw);
