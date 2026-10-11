/*
 * Semaphore abandonment test suite.
 */

#include <_test.h>
#include <common.h>
#include <scheduler.h>
#include <spinlock.h>
#include <arch.h>
#include <log.h>
#include <thread.h>
#include <obj.h>
#include <sem.h>
#include <vmm.h>
#include <assert.h>
#include <errno.h>
#include <string.h>

/* ADJUST: replace with whatever gives up the CPU to other threads. */
#ifndef SEMTEST_YIELD
#define SEMTEST_YIELD() Schedule()
#endif

/*
 * One Schedule() call walks every runnable thread once, and your kernel logs
 * every hop, so a "spin" is expensive. A real handoff completes within a
 * pass or two, so these are generous.
 */
#define SPIN_WARN           200L
#define SPIN_LIMIT          3000L
#define BLOCKED_PROBE_YIELDS 20
#define MAX_SEMS_PER_WAITER 8

/* ------------------------------------------------------------------------ */
/* Helpers                                                                   */
/* ------------------------------------------------------------------------ */

#define POOL_SIZE 12

/*
 * How far the most recent job got on its worker:
 *   0 not picked up yet, 3 picked up and about to call AcquireSemFromMany,
 *   4 AcquireSemFromMany returned.
 */
static volatile int g_waiter_stage;

static int PoolStarted(void);

/*
 * NOTE: `cond` is evaluated exactly once per iteration. Some callers pass a
 * condition with a side effect (claiming a worker), so it must never be
 * evaluated twice in a row.
 */
#define SpinUntil(cond)                                                      \
    do {                                                                     \
        long _spins = 0;                                                     \
        for (;;) {                                                           \
            if (cond) {                                                      \
                break;                                                       \
            }                                                                \
            if (_spins == 0) {                                               \
                TRACE("  wait: %s\n", #cond);                        \
            }                                                                \
            SEMTEST_YIELD();                                                 \
            if (++_spins == SPIN_WARN) {                                     \
                LogPrintf("sem test: STILL WAITING for: %s (workers started %d, job stage %d)\n", #cond, PoolStarted(), g_waiter_stage); \
            }                                                                \
            if (_spins > SPIN_LIMIT) {                                       \
                LogPrintf("sem test: TIMED OUT waiting for: %s (workers started %d, job stage %d)\n", #cond, PoolStarted(), g_waiter_stage); \
                assert(0);                                                   \
                break;                                                       \
            }                                                                \
        }                                                                    \
    } while (0)

/* Volatile reads of the queue pointers so busy-wait loops can't be hoisted. */
static void* QueueStart(struct sem* s) {
    return *(void* volatile*) &s->waiting_list_start;
}

static void* QueueEnd(struct sem* s) {
    return *(void* volatile*) &s->waiting_list_end;
}

struct waiter {
    /* Inputs. */
    struct sem** sems;
    int count;
    int64_t timeout;

    /* Outputs, written by the waiter thread. */
    struct thread* volatile thr;
    volatile int done;
    volatile int result;
    volatile int selected;
    void* volatile wsc_after;   /* thr->waiting_sem_or_clot once woken */
    void* volatile nws_after;   /* thr->next_waiting_sem once woken */
};

/*
 * Worker pool. Threads can't terminate, so each worker lives forever:
 *   idle:  job == NULL, busy == 0
 *   claim: test thread sets busy = 1, then job = <waiter>
 *   run:   worker takes the job (job = NULL), runs it, sets busy = 0
 * Only the test thread ever claims, so claiming needs no atomics.
 */
struct worker {
    struct waiter* volatile job;
    volatile int busy;
    volatile int stage;   /* 0 not claimed by a thread yet, 2 claimed and running */
    int idle_logs;        /* how many idle-loop Schedule calls have been logged */
};

static struct worker g_pool[POOL_SIZE];
static int g_pool_created;

static int PoolStarted(void) {
    int n = 0;
    for (int i = 0; i < g_pool_created; ++i) {
        if (g_pool[i].stage == 2) {
            ++n;
        }
    }
    return n;
}

/* Runs one blocking acquire on the calling worker thread. */
static void RunWaiter(struct waiter* w) {
    struct thread* me = GetCurrentThread();
    int sel = -1;

    w->thr = me;
    TRACE("  waiter %p: entered, calling AcquireSemFromMany\n", (void*) me);
    g_waiter_stage = 3;
    int r = AcquireSemFromMany(w->sems, w->count, w->timeout, &sel);
    g_waiter_stage = 4;
    TRACE("  waiter %p: woke, result=%d selected=%d\n", (void*) me, r, sel);

    w->result = r;
    w->selected = sel;
    w->wsc_after = (void*) me->waiting_sem_or_clot;
    w->nws_after = (void*) me->next_waiting_sem;

    /* Must be last: the test thread may reclaim *w as soon as this is set. */
    w->done = 1;
}

/*
 * Workers do NOT read their context argument. The first run showed a page
 * fault at exactly the top of the new thread's stack (see the note on
 * TestContextArgument): the thread entry's parameter appears to be read from
 * one slot above where CreateThread() stored it. Until that's fixed in the
 * kernel, each worker instead claims the next free pool slot by counting.
 * Slots are initialised before any worker can run, and the claim is a plain
 * increment, which is fine only because scheduling is cooperative. With
 * preemption enabled this needs to be atomic.
 */
static int g_worker_claims;

static void WorkerEntry(void* ctx) {
    (void) ctx;     /* deliberately unused, see above */
    BeginNewThread();

    int idx = g_worker_claims++;
    CHECK(idx >= 0 && idx < g_pool_created);
    struct worker* wk = &g_pool[idx];

    wk->stage = 2;
    TRACE("  worker %d: past BeginNewThread, entering idle loop\n", idx);

    /* Never returns: there's no thread termination yet. */
    for (;;) {
        struct waiter* w;
        while ((w = wk->job) == NULL) {
            if (wk->idle_logs < 2) {
                TRACE("  worker %d: idle, calling Schedule\n", (int) (wk - g_pool));
            }
            SEMTEST_YIELD();
            if (wk->idle_logs < 2) {
                TRACE("  worker %d: idle Schedule returned\n", (int) (wk - g_pool));
                ++wk->idle_logs;
            }
        }
        wk->job = NULL;
        RunWaiter(w);      /* sets w->done last; don't touch w afterwards */
        wk->busy = 0;
    }
}

static void CreateWorker(int i) {
    CHECK(i == g_pool_created && i < POOL_SIZE);
    g_pool[i].job = NULL;
    g_pool[i].busy = 0;
    g_pool[i].stage = 0;
    g_pool[i].idle_logs = 0;
    g_pool_created = i + 1;
    TRACE("  CreateWorker %d: calling CreateThread\n", i);
    CreateThread(GetKernelVas(), WorkerEntry, NULL);
    TRACE("  CreateWorker %d: CreateThread returned\n", i);
}

static void EnsurePool(void) {
    while (g_pool_created < POOL_SIZE) {
        CreateWorker(g_pool_created);
    }
    for (int i = 0; i < POOL_SIZE; ++i) {
        SpinUntil(g_pool[i].stage == 2);
    }
}

static struct worker* ClaimIdleWorker(void) {
    for (int i = 0; i < g_pool_created; ++i) {
        if (g_pool[i].stage == 2 && !g_pool[i].busy) {
            g_pool[i].busy = 1;
            return &g_pool[i];
        }
    }
    return NULL;
}

/*
 * Starts a thread that blocks on `sems`, and returns only once that thread
 * is visibly queued on every one of them. Requires each sem to be in a
 * state where acquiring blocks (count == max).
 */
static void SpawnWaiter(struct waiter* w, struct sem** sems, int count) {
    void* old_ends[MAX_SEMS_PER_WAITER];
    CHECK(count >= 1 && count <= MAX_SEMS_PER_WAITER);

    memset(w, 0, sizeof(*w));
    w->sems = sems;
    w->count = count;
    w->timeout = TIMEOUT_INFINITE;
    w->selected = -1;

    for (int i = 0; i < count; ++i) {
        old_ends[i] = QueueEnd(sems[i]);
    }

    EnsurePool();

    struct worker* wk;
    SpinUntil((wk = ClaimIdleWorker()) != NULL);   /* fails if >POOL_SIZE waiters */

    g_waiter_stage = 0;
    TRACE("  SpawnWaiter: handing job to worker\n");
    wk->job = w;
    TRACE("  SpawnWaiter: job handed over, waiting for it to queue\n");

    for (int i = 0; i < count; ++i) {
        SpinUntil(QueueEnd(sems[i]) != old_ends[i]);
    }
    TRACE("  SpawnWaiter: waiter is queued\n");
    CHECK(!w->done);
    CHECK(w->thr != NULL);
}

static struct sem* NewBlockingSem(void) {
    /* count == max, so any acquire has to wait. */
    return CreateSem(1, 1);
}

static void CheckQueueEmpty(struct sem* s) {
    CHECK(QueueStart(s) == NULL);
    CHECK(QueueEnd(s) == NULL);
}

static void CheckQueueSingleEntry(struct sem* s) {
    CHECK(QueueStart(s) != NULL);
    CHECK(QueueStart(s) == QueueEnd(s));
}

static void CheckAbandoned(struct waiter* w) {
    TRACE("  expect: waiter %p abandoned\n", (void*) w->thr);
    SpinUntil(w->done);
    CHECK(w->result == EABANDONED);
    CHECK(w->selected == -1);
    CHECK(w->wsc_after == NULL);
    CHECK(w->nws_after == NULL);
}

static void CheckAcquired(struct waiter* w, int expected_selected) {
    TRACE("  expect: waiter %p acquired, selected=%d\n", (void*) w->thr, expected_selected);
    SpinUntil(w->done);
    CHECK(w->result == 0);
    CHECK(w->selected == expected_selected);
    CHECK(w->wsc_after == NULL);
    CHECK(w->nws_after == NULL);
}

/* Give a wrongly-woken thread a chance to show itself, then assert it hasn't. */
static void CheckStillBlocked(struct waiter* w) {
    TRACE("  expect: waiter %p still blocked\n", (void*) w->thr);
    for (int i = 0; i < BLOCKED_PROBE_YIELDS; ++i) {
        SEMTEST_YIELD();
    }
    CHECK(!w->done);
}

/* A sem that's been abandoned must still be a working sem. */
static void CheckSemUsable(struct sem* s) {
    CHECK(ReleaseSem(s) == 0);                        /* empty queue: count-- */
    CHECK(s->count == s->max - 1);
    CHECK(AcquireSem(s, TIMEOUT_INSTANT) == 0);       /* count++ */
    CHECK(s->count == s->max);
    CHECK(AcquireSem(s, TIMEOUT_INSTANT) == EAGAIN);
}

/* ------------------------------------------------------------------------ */
/* Basic cases                                                               */
/* ------------------------------------------------------------------------ */

static void TestAbandonNoWaiters(void) {
    struct sem* s = CreateSem(2, 1);

    AbandonSem(s);
    CHECK(s->count == 1);
    CheckQueueEmpty(s);

    AbandonSem(s);   /* twice is fine */
    CHECK(s->count == 1);
    CheckQueueEmpty(s);

    CHECK(AcquireSem(s, TIMEOUT_INSTANT) == 0);
    CHECK(s->count == 2);
    CHECK(AcquireSem(s, TIMEOUT_INSTANT) == EAGAIN);

    DerefObject(s);
}

static void TestAbandonSingleWaiter(void) {
    struct sem* s = NewBlockingSem();
    struct sem* sems[1] = { s };
    struct waiter w;

    SpawnWaiter(&w, sems, 1);
    CheckQueueSingleEntry(s);
    CHECK(QueueStart(s) == (void*) w.thr);

    TRACE("  calling AbandonSem\n");
    AbandonSem(s);
    TRACE("  AbandonSem returned, waiting for waiter to finish\n");

    CheckAbandoned(&w);
    CheckQueueEmpty(s);
    CHECK(s->count == s->max);   /* abandon doesn't touch the count */
    CheckSemUsable(s);

    DerefObject(s);
}

static void TestAbandonManyWaitersOneSem(void) {
    enum { N = 6 };
    struct sem* s = NewBlockingSem();
    struct sem* sems[1] = { s };
    struct waiter w[N];

    for (int i = 0; i < N; ++i) {
        SpawnWaiter(&w[i], sems, 1);
    }
    CHECK(QueueEnd(s) == (void*) w[N - 1].thr);

    AbandonSem(s);

    for (int i = 0; i < N; ++i) {
        CheckAbandoned(&w[i]);
    }
    CheckQueueEmpty(s);
    CheckSemUsable(s);

    DerefObject(s);
}

static void TestAbandonTwiceWithWaiters(void) {
    struct sem* s = NewBlockingSem();
    struct sem* sems[1] = { s };
    struct waiter w[3];

    for (int i = 0; i < 3; ++i) {
        SpawnWaiter(&w[i], sems, 1);
    }

    AbandonSem(s);
    AbandonSem(s);   /* second is a no-op */

    for (int i = 0; i < 3; ++i) {
        CheckAbandoned(&w[i]);
    }
    CheckQueueEmpty(s);

    DerefObject(s);
}

/* ------------------------------------------------------------------------ */
/* Multi-sem waiters (wait clots)                                            */
/* ------------------------------------------------------------------------ */

static void MultiTwoSemsImpl(int abandon_idx) {
    struct sem* s[2] = { NewBlockingSem(), NewBlockingSem() };
    struct waiter w;
    int other = 1 - abandon_idx;

    SpawnWaiter(&w, s, 2);
    CheckQueueSingleEntry(s[0]);
    CheckQueueSingleEntry(s[1]);

    AbandonSem(s[abandon_idx]);

    CheckAbandoned(&w);
    CheckQueueEmpty(s[0]);
    CheckQueueEmpty(s[1]);   /* must be pulled off the other queue too */

    /* Abandoning the other sem now must be a no-op (no stale entry). */
    AbandonSem(s[other]);
    CHECK(s[0]->count == 1);
    CHECK(s[1]->count == 1);

    /* A stale entry would make this release try to wake a finished thread. */
    CheckSemUsable(s[other]);
    CheckSemUsable(s[abandon_idx]);

    DerefObject(s[0]);
    DerefObject(s[1]);
}

static void TestMultiTwoSemsAbandonFirst(void)  { MultiTwoSemsImpl(0); }
static void TestMultiTwoSemsAbandonSecond(void) { MultiTwoSemsImpl(1); }

static void MultiThreeSemsImpl(int abandon_idx) {
    struct sem* s[3] = { NewBlockingSem(), NewBlockingSem(), NewBlockingSem() };
    struct waiter w;

    SpawnWaiter(&w, s, 3);

    AbandonSem(s[abandon_idx]);

    CheckAbandoned(&w);
    for (int i = 0; i < 3; ++i) {
        CheckQueueEmpty(s[i]);
        CheckSemUsable(s[i]);
    }
    for (int i = 0; i < 3; ++i) {
        DerefObject(s[i]);
    }
}

static void TestMultiThreeSemsAbandonFirst(void)  { MultiThreeSemsImpl(0); }
static void TestMultiThreeSemsAbandonMiddle(void) { MultiThreeSemsImpl(1); }
static void TestMultiThreeSemsAbandonLast(void)   { MultiThreeSemsImpl(2); }

/* Several clots all sitting on the same pair of sems. */
static void TestManyClotsSamePair(void) {
    enum { N = 5 };
    struct sem* s[2] = { NewBlockingSem(), NewBlockingSem() };
    struct waiter w[N];

    for (int i = 0; i < N; ++i) {
        SpawnWaiter(&w[i], s, 2);
    }

    AbandonSem(s[0]);

    for (int i = 0; i < N; ++i) {
        CheckAbandoned(&w[i]);
    }
    CheckQueueEmpty(s[0]);
    CheckQueueEmpty(s[1]);

    DerefObject(s[0]);
    DerefObject(s[1]);
}

/*
 * Overlapping sets:
 *   w1 on {A,B}, w2 on {B,C}, w3 on {A,C}
 * Abandon B kills w1 and w2 only; then abandon C kills w3.
 */
static void TestOverlappingWaitSets(void) {
    struct sem* A = NewBlockingSem();
    struct sem* B = NewBlockingSem();
    struct sem* C = NewBlockingSem();
    struct sem* set1[2] = { A, B };
    struct sem* set2[2] = { B, C };
    struct sem* set3[2] = { A, C };
    struct waiter w1, w2, w3;

    SpawnWaiter(&w1, set1, 2);
    SpawnWaiter(&w2, set2, 2);
    SpawnWaiter(&w3, set3, 2);

    AbandonSem(B);

    CheckAbandoned(&w1);
    CheckAbandoned(&w2);
    CheckStillBlocked(&w3);
    CheckQueueEmpty(B);
    CheckQueueSingleEntry(A);   /* only w3 left */
    CheckQueueSingleEntry(C);

    AbandonSem(C);

    CheckAbandoned(&w3);
    CheckQueueEmpty(A);
    CheckQueueEmpty(B);
    CheckQueueEmpty(C);

    DerefObject(A);
    DerefObject(B);
    DerefObject(C);
}

/* ------------------------------------------------------------------------ */
/* Removal from different positions in the *other* queue                     */
/* ------------------------------------------------------------------------ */

/* B: w1(single) -> w2(clot A,B) -> w3(single). Abandon A removes the middle. */
static void TestClotInMiddleOfThreads(void) {
    struct sem* A = NewBlockingSem();
    struct sem* B = NewBlockingSem();
    struct sem* b_only[1] = { B };
    struct sem* ab[2] = { A, B };
    struct waiter w1, w2, w3;

    SpawnWaiter(&w1, b_only, 1);
    SpawnWaiter(&w2, ab, 2);
    SpawnWaiter(&w3, b_only, 1);

    AbandonSem(A);

    CheckAbandoned(&w2);
    CheckQueueEmpty(A);
    CheckStillBlocked(&w1);
    CheckStillBlocked(&w3);
    CHECK(QueueStart(B) == (void*) w1.thr);
    CHECK(QueueEnd(B) == (void*) w3.thr);

    /* B's list must have been relinked: w1 -> w3, in order. */
    CHECK(ReleaseSem(B) == 0);
    CheckAcquired(&w1, 0);
    CHECK(QueueStart(B) == (void*) w3.thr);
    CHECK(!w3.done);

    CHECK(ReleaseSem(B) == 0);
    CheckAcquired(&w3, 0);
    CheckQueueEmpty(B);

    DerefObject(A);
    DerefObject(B);
}

/* B: w1(single) -> w2(clot A,B). The removed clot is the tail of B. */
static void TestClotAtTailOfOtherQueue(void) {
    struct sem* A = NewBlockingSem();
    struct sem* B = NewBlockingSem();
    struct sem* b_only[1] = { B };
    struct sem* ab[2] = { A, B };
    struct waiter w1, w2;

    SpawnWaiter(&w1, b_only, 1);
    SpawnWaiter(&w2, ab, 2);

    AbandonSem(A);

    CheckAbandoned(&w2);
    CheckQueueEmpty(A);
    CheckQueueSingleEntry(B);
    CHECK(QueueStart(B) == (void*) w1.thr);
    CHECK(QueueEnd(B) == (void*) w1.thr);   /* tail pointer repaired */

    CHECK(ReleaseSem(B) == 0);
    CheckAcquired(&w1, 0);
    CheckQueueEmpty(B);

    DerefObject(A);
    DerefObject(B);
}

/* B: w1(clot A,B) -> w2(single). The removed clot is the head of B. */
static void TestClotAtHeadOfOtherQueue(void) {
    struct sem* A = NewBlockingSem();
    struct sem* B = NewBlockingSem();
    struct sem* b_only[1] = { B };
    struct sem* ab[2] = { A, B };
    struct waiter w1, w2;

    SpawnWaiter(&w1, ab, 2);
    SpawnWaiter(&w2, b_only, 1);

    AbandonSem(A);

    CheckAbandoned(&w1);
    CheckQueueEmpty(A);
    CheckQueueSingleEntry(B);
    CHECK(QueueStart(B) == (void*) w2.thr);   /* head advanced past the clot */
    CHECK(QueueEnd(B) == (void*) w2.thr);

    CHECK(ReleaseSem(B) == 0);
    CheckAcquired(&w2, 0);
    CheckQueueEmpty(B);

    DerefObject(A);
    DerefObject(B);
}

/*
 * B: w1(clot C,B) -> w2(clot A,B) -> w3(single).
 * Abandon A removes a clot whose predecessor is also a clot.
 */
static void TestClotBetweenClots(void) {
    struct sem* A = NewBlockingSem();
    struct sem* B = NewBlockingSem();
    struct sem* C = NewBlockingSem();
    struct sem* cb[2] = { C, B };
    struct sem* ab[2] = { A, B };
    struct sem* b_only[1] = { B };
    struct waiter w1, w2, w3;

    SpawnWaiter(&w1, cb, 2);
    SpawnWaiter(&w2, ab, 2);
    SpawnWaiter(&w3, b_only, 1);

    AbandonSem(A);

    CheckAbandoned(&w2);
    CheckQueueEmpty(A);
    CheckStillBlocked(&w1);
    CheckStillBlocked(&w3);
    CHECK(QueueEnd(B) == (void*) w3.thr);

    /* Release B: w1 wins via its second sem (index 1), leaves C's queue. */
    CHECK(ReleaseSem(B) == 0);
    CheckAcquired(&w1, 1);
    CheckQueueEmpty(C);
    CHECK(!w3.done);

    CHECK(ReleaseSem(B) == 0);
    CheckAcquired(&w3, 0);
    CheckQueueEmpty(B);

    AbandonSem(C);   /* no-op: w1 must not still be queued there */
    CheckQueueEmpty(C);

    DerefObject(A);
    DerefObject(B);
    DerefObject(C);
}

/* ------------------------------------------------------------------------ */
/* Interaction with normal release                                           */
/* ------------------------------------------------------------------------ */

/* A waiter that already won via ReleaseSem must not be hit by a later abandon. */
static void TestReleaseThenAbandon(void) {
    struct sem* s[2] = { NewBlockingSem(), NewBlockingSem() };
    struct waiter w;

    SpawnWaiter(&w, s, 2);

    CHECK(ReleaseSem(s[1]) == 0);
    CheckAcquired(&w, 1);
    CheckQueueEmpty(s[0]);   /* removed from the sem it didn't win */
    CheckQueueEmpty(s[1]);

    AbandonSem(s[0]);
    AbandonSem(s[1]);
    CHECK(w.result == 0);    /* result must not change after the fact */

    DerefObject(s[0]);
    DerefObject(s[1]);
}

/* T1 is released normally, then T2 and T3 are abandoned. */
static void TestReleaseOneThenAbandonRest(void) {
    struct sem* s = NewBlockingSem();
    struct sem* sems[1] = { s };
    struct waiter w[3];

    for (int i = 0; i < 3; ++i) {
        SpawnWaiter(&w[i], sems, 1);
    }

    CHECK(ReleaseSem(s) == 0);
    CheckAcquired(&w[0], 0);   /* FIFO: first waiter wins */
    CheckStillBlocked(&w[1]);
    CheckStillBlocked(&w[2]);

    AbandonSem(s);

    CheckAbandoned(&w[1]);
    CheckAbandoned(&w[2]);
    CHECK(w[0].result == 0);
    CheckQueueEmpty(s);

    DerefObject(s);
}

/* Waiters on a different sem must not be disturbed. */
static void TestUnrelatedWaiterUnaffected(void) {
    struct sem* A = NewBlockingSem();
    struct sem* B = NewBlockingSem();
    struct sem* a_only[1] = { A };
    struct sem* b_only[1] = { B };
    struct waiter wa, wb;

    SpawnWaiter(&wa, a_only, 1);
    SpawnWaiter(&wb, b_only, 1);

    AbandonSem(A);

    CheckAbandoned(&wa);
    CheckStillBlocked(&wb);
    CheckQueueSingleEntry(B);
    CHECK(QueueStart(B) == (void*) wb.thr);

    CHECK(ReleaseSem(B) == 0);
    CheckAcquired(&wb, 0);

    DerefObject(A);
    DerefObject(B);
}

/* ------------------------------------------------------------------------ */
/* Destruction paths                                                         */
/* ------------------------------------------------------------------------ */

/* Dropping the last reference to a sem with waiters must abandon them. */
static void TestDerefDestroyWithWaiters(void) {
    enum { N = 3 };
    struct sem* s = NewBlockingSem();
    struct sem* sems[1] = { s };
    struct waiter w[N];

    for (int i = 0; i < N; ++i) {
        SpawnWaiter(&w[i], sems, 1);
    }

    DerefObject(s);   /* s is freed from here on: don't touch it */

    for (int i = 0; i < N; ++i) {
        CheckAbandoned(&w[i]);
    }
}

/* Destroying one sem of a multi-wait must leave the other fully intact. */
static void TestDerefDestroyMultiWaiter(void) {
    struct sem* s[2] = { NewBlockingSem(), NewBlockingSem() };
    struct waiter w;

    SpawnWaiter(&w, s, 2);

    DerefObject(s[0]);

    CheckAbandoned(&w);
    CheckQueueEmpty(s[1]);
    CheckSemUsable(s[1]);

    DerefObject(s[1]);
}

static struct sem g_static_sem;

static void TestDestroyStaticSemWithWaiters(void) {
    struct sem* p = &g_static_sem;
    struct waiter w[2];

    InitStaticSem(&g_static_sem, 1, 1);

    SpawnWaiter(&w[0], &p, 1);
    SpawnWaiter(&w[1], &p, 1);

    DestroyStaticSem(&g_static_sem);

    CheckAbandoned(&w[0]);
    CheckAbandoned(&w[1]);
    CheckQueueEmpty(&g_static_sem);
}

/* Static sem + heap sem in one wait set. */
static void TestDestroyStaticSemMultiWaiter(void) {
    struct sem* heap_sem = NewBlockingSem();
    struct sem* sems[2];
    struct waiter w;

    InitStaticSem(&g_static_sem, 1, 1);
    sems[0] = &g_static_sem;
    sems[1] = heap_sem;

    SpawnWaiter(&w, sems, 2);

    DestroyStaticSem(&g_static_sem);

    CheckAbandoned(&w);
    CheckQueueEmpty(&g_static_sem);
    CheckQueueEmpty(heap_sem);
    CheckSemUsable(heap_sem);

    DerefObject(heap_sem);
}

/* ------------------------------------------------------------------------ */
/* Randomised stress                                                         */
/* ------------------------------------------------------------------------ */

#define STRESS_SEMS    5
#define STRESS_THREADS 12
#define STRESS_ROUNDS  30

static struct waiter g_stress_waiters[STRESS_THREADS];
static struct sem* g_stress_subsets[STRESS_THREADS][STRESS_SEMS];

static uint32_t NextRand(uint32_t* state) {
    uint32_t x = *state;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    *state = x;
    return x;
}

/*
 * Each round: spawn threads that each wait on a random distinct subset of the
 * sems, normally release a few of them, then abandon every sem in random
 * order. Afterwards every thread must have finished with either 0 (it won a
 * release) or EABANDONED, and all queues must be empty.
 */
static void StressRound(uint32_t seed) {
    struct sem* sems[STRESS_SEMS];
    uint32_t rng = seed != 0 ? seed : 1;

    for (int i = 0; i < STRESS_SEMS; ++i) {
        sems[i] = NewBlockingSem();
    }

    for (int t = 0; t < STRESS_THREADS; ++t) {
        int idx[STRESS_SEMS];
        for (int k = 0; k < STRESS_SEMS; ++k) {
            idx[k] = k;
        }
        int n = 1 + (int) (NextRand(&rng) % STRESS_SEMS);
        for (int k = 0; k < n; ++k) {
            int j = k + (int) (NextRand(&rng) % (uint32_t) (STRESS_SEMS - k));
            int tmp = idx[k];
            idx[k] = idx[j];
            idx[j] = tmp;
            g_stress_subsets[t][k] = sems[idx[k]];
        }
        SpawnWaiter(&g_stress_waiters[t], g_stress_subsets[t], n);
    }

    /* Release phase: only release sems that actually have someone waiting. */
    int woken = 0;
    int releases = (int) (NextRand(&rng) % (STRESS_THREADS / 2 + 1));
    for (int r = 0; r < releases; ++r) {
        int start = (int) (NextRand(&rng) % STRESS_SEMS);
        struct sem* target = NULL;
        for (int k = 0; k < STRESS_SEMS; ++k) {
            struct sem* cand = sems[(start + k) % STRESS_SEMS];
            if (QueueStart(cand) != NULL) {
                target = cand;
                break;
            }
        }
        if (target == NULL) {
            break;
        }
        CHECK(ReleaseSem(target) == 0);
        ++woken;
    }

    /* Abandon phase, random order. */
    int order[STRESS_SEMS];
    for (int i = 0; i < STRESS_SEMS; ++i) {
        order[i] = i;
    }
    for (int i = STRESS_SEMS - 1; i > 0; --i) {
        int j = (int) (NextRand(&rng) % (uint32_t) (i + 1));
        int tmp = order[i];
        order[i] = order[j];
        order[j] = tmp;
    }
    for (int i = 0; i < STRESS_SEMS; ++i) {
        AbandonSem(sems[order[i]]);
    }

    /* Verify. */
    int acquired = 0;
    for (int t = 0; t < STRESS_THREADS; ++t) {
        struct waiter* w = &g_stress_waiters[t];
        SpinUntil(w->done);
        if (w->result == 0) {
            ++acquired;
            CHECK(w->selected >= 0 && w->selected < w->count);
        } else {
            CHECK(w->result == EABANDONED);
            CHECK(w->selected == -1);
        }
        CHECK(w->wsc_after == NULL);
        CHECK(w->nws_after == NULL);
    }
    CHECK(acquired == woken);

    for (int i = 0; i < STRESS_SEMS; ++i) {
        CheckQueueEmpty(sems[i]);
        DerefObject(sems[i]);
    }
}

static void TestStressRandomWaitSets(void) {
    for (int round = 0; round < STRESS_ROUNDS; ++round) {
        StressRound(0x9E3779B9u * (uint32_t) (round + 1));
    }
}

/* ------------------------------------------------------------------------ */
/* Runner                                                                    */
/* ------------------------------------------------------------------------ */

void RunSemAbandonTests(void) {
    /* Basic */
    RUN_TEST(TestAbandonNoWaiters);
    RUN_TEST(TestAbandonSingleWaiter);
    RUN_TEST(TestAbandonManyWaitersOneSem);
    RUN_TEST(TestAbandonTwiceWithWaiters);

    /* Multi-sem waiters */
    RUN_TEST(TestMultiTwoSemsAbandonFirst);
    RUN_TEST(TestMultiTwoSemsAbandonSecond);
    RUN_TEST(TestMultiThreeSemsAbandonFirst);
    RUN_TEST(TestMultiThreeSemsAbandonMiddle);
    RUN_TEST(TestMultiThreeSemsAbandonLast);
    RUN_TEST(TestManyClotsSamePair);
    RUN_TEST(TestOverlappingWaitSets);

    /* Queue-position coverage for CleanupAfterSem */
    RUN_TEST(TestClotInMiddleOfThreads);
    RUN_TEST(TestClotAtTailOfOtherQueue);
    RUN_TEST(TestClotAtHeadOfOtherQueue);
    RUN_TEST(TestClotBetweenClots);

    /* Interaction with release */
    RUN_TEST(TestReleaseThenAbandon);
    RUN_TEST(TestReleaseOneThenAbandonRest);
    RUN_TEST(TestUnrelatedWaiterUnaffected);

    /* Destruction paths */
    RUN_TEST(TestDerefDestroyWithWaiters);
    RUN_TEST(TestDerefDestroyMultiWaiter);
    RUN_TEST(TestDestroyStaticSemWithWaiters);
    RUN_TEST(TestDestroyStaticSemMultiWaiter);

    /* Randomised */
    RUN_TEST(TestStressRandomWaitSets);

    LogPrintf("sem abandon tests: ALL PASSED\n");
}