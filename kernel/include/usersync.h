#pragma once

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include <common.h>

/*
 * Semaphores, mutexes and reader/writer locks for usermode. The system call numbers are in syscall.h, and the
 * implementations in syscall/usersem.c, usermutex.c and userrwlock.c, on top of the kernel's own (sem.h,
 * mutex.h, rwlock.h).
 *
 * They are objects like any other, so the handle you get back is closed with Deref() (uobj.h) when you have
 * finished with it, and can be handed on with Ref(). Closing it does not wake anyone who is waiting on the
 * object, nor release it if you were holding it: a thread that is waiting keeps the object alive until it has
 * stopped waiting. Nothing cleans up after a thread that exits while it holds one.
 *
 * Nothing here is tied to a thread. A lock can be released by a different thread from the one that acquired it,
 * and acquiring it again from the thread that holds it waits for itself, for ever. (These are the same locks the
 * kernel uses inside itself, with the same behaviour. The kernel never takes one that usermode has, so a program
 * can only ever hold up itself.)
 *
 * The calls that can fail set the thread's last error (see GetLastError()) and return 0 or false. The errors:
 *
 *      EINVAL          not a handle to the right kind of object (or one that has been closed), or a bad argument
 *      ENOMEM          creating one: out of memory
 *      EAGAIN          an acquire with a timeout of 0, and it would have had to wait
 *      ETIMEDOUT       an acquire with any other timeout, and the time ran out. Nothing is held
 *      EOVERFLOW       a release that there is nothing to take: for a semaphore, one that would make more than `max`
 *                      free; for a mutex, one that is not held; for a reader/writer lock, one with no reader (or no
 *                      writer) in. Nothing changes. This is only noticed when nothing at all is held. Releasing
 *                      something that another thread holds goes through, and takes it away from them.
 *
 * Timeouts are in nanoseconds, from the moment of the call, and are a minimum rounded up to a tick of the
 * system timer (40ms at the moment). 0 means do not wait at all: succeed straight away or fail with EAGAIN.
 * TIMEOUT_INFINITE (or UINT64_MAX) waits for as long as it takes. For a reader/writer lock the time is for the
 * whole acquire, and a failed one leaves the lock as it found it.
 */

/* Opaque, as with every handle: only ever give these back to the kernel. */
struct semaphore_handle;
struct mutex_handle;
struct rwlock_handle;
typedef struct semaphore_handle* semaphore_t;
typedef struct mutex_handle* mutex_t;
typedef struct rwlock_handle* rwlock_t;

/*
 * A counting semaphore that lets up to `max` threads through at once. It starts with `initial` of them free to
 * be taken straight away: CreateSemaphore(1, 1) is a lock that is not held, CreateSemaphore(1, 0) one that
 * is, and CreateSemaphore(n, 0) is the sort that counts up to n as things are made available, with every
 * Release() making one, and every Acquire() using one.
 *
 * `max` has to be at least 1, and `initial` between 0 and `max`.
 *
 * Returns the handle, or 0 with the last error set.
 */
semaphore_t CreateSemaphore(int max, int initial);

/* Takes one. Returns true if it has, or false with the last error set. */
int AcquireSemaphore(semaphore_t sem, uint64_t timeout_ns);

/* Gives one back, which wakes the thread that has been waiting the longest. */
int ReleaseSemaphore(semaphore_t sem);

/* A mutex starts out not held. */
mutex_t CreateMutex(void);
int AcquireMutex(mutex_t mtx, uint64_t timeout_ns);
int ReleaseMutex(mutex_t mtx);

/*
 * Any number of threads can hold a reader/writer lock for reading at once, or just one can hold it for writing.
 * It is fair, and doesn't let readers starve a writer: once a writer is waiting, readers that arrive after it
 * queue up behind it. See rwlock.h for the rest of the details, which all apply here. In particular it can't be
 * upgraded, and a thread that holds it for reading and asks for it again, for reading or for writing, can
 * deadlock behind a writer that arrived in between. Release it with the call that matches the one that took it.
 */
rwlock_t CreateRwLock(void);
int AcquireRwLockRead(rwlock_t rw, uint64_t timeout_ns);
int AcquireRwLockWrite(rwlock_t rw, uint64_t timeout_ns);
int ReleaseRwLockRead(rwlock_t rw);
int ReleaseRwLockWrite(rwlock_t rw);
