#include <common.h>
#include <obj.h>
#include <heap.h>
#include <mutex.h>
#include <rwlock.h>
#include <timer.h>
#include <errno.h>
#include <assert.h>

/*
 * The design is the "no-starve" readers-writers solution: a turnstile in front
 * of a room, where the readers share one key to the room.
 *
 *   write:  gate.lock;  room.lock;  ... ;  gate.unlock;  room.unlock
 *   read:   gate.lock;  gate.unlock;
 *           readers_lock.lock;  if (readers++ == 0) room.lock;  readers_lock.unlock
 *           ...
 *           readers_lock.lock;  if (--readers == 0) room.unlock;  readers_lock.unlock
 *
 * The rest of this file is mostly making the timeouts work: each step has to
 * be given only the time that is left, and a step that fails has to put back
 * what the earlier steps took.
 */

struct rw_timeout {
    int64_t timeout;
    uint64_t deadline;      /* Only if timeout > 0. */
};

static struct rw_timeout KeStartRwTimeout(int64_t timeout) {
    struct rw_timeout t = { .timeout = timeout, .deadline = 0 };
    if (timeout > 0) {
        t.deadline = KeGetTimeSinceBoot() + (uint64_t) timeout;
    }
    return t;
}

/* How long the next step may wait. */
static int64_t KeRwTimeRemaining(const struct rw_timeout* t) {
    if (t->timeout <= 0) {
        /* TIMEOUT_INSTANT or TIMEOUT_INFINITE: nothing to count down. */
        return t->timeout;
    }
    uint64_t now = KeGetTimeSinceBoot();
    if (now >= t->deadline) {
        /* Out of time, but still make one attempt that doesn't wait, so that
         * a lock that is free by now isn't reported as taken. */
        return TIMEOUT_INSTANT;
    }
    return (int64_t) (t->deadline - now);
}

export int KeAcquireRwLockWrite(struct rwlock* rw, int64_t timeout) {
    struct rw_timeout t = KeStartRwTimeout(timeout);

    int res = KeAcquireMutex(&rw->gate, KeRwTimeRemaining(&t));
    if (res != 0) {
        return res;
    }

    res = KeAcquireMutex(&rw->room, KeRwTimeRemaining(&t));
    if (res != 0) {
        KeReleaseMutex(&rw->gate);
        return res;
    }
    return 0;
}

export int KeReleaseRwLockWrite(struct rwlock* rw) {
    /* The gate first: whoever is queued behind us can start lining up for the
     * room, which they will get as soon as we hand that over. */
    KeReleaseMutex(&rw->gate);
    KeReleaseMutex(&rw->room);
    return 0;
}

export int KeAcquireRwLockRead(struct rwlock* rw, int64_t timeout) {
    struct rw_timeout t = KeStartRwTimeout(timeout);

    /* Wait our turn behind any writer that is already waiting. */
    int res = KeAcquireMutex(&rw->gate, KeRwTimeRemaining(&t));
    if (res != 0) {
        return res;
    }
    KeReleaseMutex(&rw->gate);

    res = KeAcquireMutex(&rw->readers_lock, KeRwTimeRemaining(&t));
    if (res != 0) {
        return res;
    }

    if (rw->readers == 0) {
        /*
         * We are the first reader in, so we need the room on behalf of all of
         * them. Any other reader arriving now waits on readers_lock, which is
         * right: they would have to wait for the room anyway. (No reader can be
         * trying to leave in the meantime, as there are none.) 'readers' isn't
         * touched until we have it, so failing here changes nothing.
         */
        res = KeAcquireMutex(&rw->room, KeRwTimeRemaining(&t));
        if (res != 0) {
            KeReleaseMutex(&rw->readers_lock);
            return res;
        }
    }

    rw->readers++;
    KeReleaseMutex(&rw->readers_lock);
    return 0;
}

export int KeReleaseRwLockRead(struct rwlock* rw) {
    /*
     * This can wait, but only for as long as another reader needs to update the
     * count. The only thing that holds readers_lock for longer is the first
     * reader waiting for the room, and while that is happening there are no
     * readers to be releasing anything.
     */
    int res = KeAcquireMutex(&rw->readers_lock, TIMEOUT_INFINITE);
    if (res != 0) {
        return res;
    }

    assert(rw->readers > 0);
    if (--rw->readers == 0) {
        KeReleaseMutex(&rw->room);
    }
    KeReleaseMutex(&rw->readers_lock);
    return 0;
}

export void KeInitStaticRwLock(struct rwlock* rw) {
    KeInitObject(rw, OBJTYPE_RWLOCK);
    KeInitStaticMutex(&rw->gate);
    KeInitStaticMutex(&rw->room);
    KeInitStaticMutex(&rw->readers_lock);
    rw->readers = 0;
}

/* The mutexes are ours, and static, so they are destroyed here rather than
 * dereferenced. Anyone still waiting on the lock is woken with EABANDONED. */
export void KeDestroyStaticRwLock(struct rwlock* rw) {
    KeDestroyStaticMutex(&rw->gate);
    KeDestroyStaticMutex(&rw->room);
    KeDestroyStaticMutex(&rw->readers_lock);
}

static void KeCleanupRwLock(void* _rw) {
    struct rwlock* rw = _rw;
    KeDestroyStaticRwLock(rw);
    FreeHeap(rw);
}

export struct rwlock* KeCreateRwLock(void) {
    struct rwlock* rw = AllocHeap(sizeof(struct rwlock));
    if (rw == NULL) {
        return NULL;
    }
    KeInitStaticRwLock(rw);
    return rw;
}

void KeInitRwLock(void) {
    KeRegisterObjectType(OBJTYPE_RWLOCK, KeCleanupRwLock);
}
