/*
 * Semaphore timeout tests.
 *
 * Call RunSemTimeoutTests() from a kernel thread once scheduling and the timer
 * are up. A failing CHECK() halts (assert); reaching the end means everything
 * passed (one soft check, see SUSPECT, only logs).
 *
 * Contract under test, for KeAcquireSem / KeAcquireSemFromMany:
 *   0          acquired (immediately, via KeReleaseSem hand-off, or from a clot)
 *   EAGAIN     sem was held and the timeout was TIMEOUT_INSTANT
 *   ETIMEDOUT  sem was held, a finite timeout was given and it ran out
 *   EABANDONED sem abandoned while waiting (checked only with a timeout set)
 * and the interplay with the timer: a thread that is woken by KeReleaseSem or
 * KeAbandonSem must leave the timer list, and a thread woken by the timer must
 * leave the sem queues.
 *
 * ASSUMPTIONS
 *   - Cooperative scheduling (no timer preemption), and nothing else advances
 *     the timer while these run: time is driven by hand with KeAdvanceTimer().
 *     When a real timer interrupt calls KeAdvanceTimer you must mask it for these.
 *   - Finite timeouts are in nanoseconds. If not, change SEMTO() below.
 *   - Threads can't terminate yet, so waiters run on a permanent worker pool.
 *   - KeCreateSem(max, initial): "initial" is the number of holders, so
 *     KeCreateSem(1, 1) is held and every acquire has to wait.
 */

#include <common.h>
#include <scheduler.h>
#include <spinlock.h>
#include <arch.h>
#include <log.h>
#include <thread.h>
#include <obj.h>
#include <sem.h>
#include <timer.h>
#include <vmm.h>
#include <_test.h>

/* Converts nanoseconds of test time into the units of a timeout argument. */
#ifndef SEMTO
#define SEMTO(ns) ((int64_t) (ns))
#endif

#define POOL_SIZE      8
#define SPIN_LIMIT     3000
#define BLOCKED_PROBE  20
#define SETTLE         3
#define HUGE_NS        (1000ULL * 1000 * 1000 * 1000 * 1000)   /* ~11 days */

static volatile int g_suspected;

#define SUSPECT(cond, what) do { \
    if (!(cond)) { g_suspected++; KeLogPrintf("SUSPECTED BUG: %s\n", what); } \
} while (0)

/* `cond` is evaluated once per iteration. */
#define SpinUntil(cond)                                                     \
    do {                                                                    \
        long _spins = 0;                                                    \
        for (;;) {                                                          \
            if (cond) break;                                                \
            if (++_spins > SPIN_LIMIT) {                                    \
                KeLogPrintf("sem timeout test: TIMED OUT waiting for: %s\n", #cond); \
                assert(0);                                                  \
                break;                                                      \
            }                                                               \
            KeSchedule();                                                     \
        }                                                                   \
    } while (0)

static void Settle(void) {
    for (int i = 0; i < SETTLE; ++i) KeSchedule();
}

static void* QueueStart(struct sem* s) { return *(void* volatile*) &s->waiting_list_start; }
static void* QueueEnd(struct sem* s)   { return *(void* volatile*) &s->waiting_list_end; }

static void CheckQueueEmpty(struct sem* s) {
    CHECK(QueueStart(s) == NULL);
    CHECK(QueueEnd(s) == NULL);
}

/* ------------------------------------------------------------------------ */
/* Worker pool                                                               */
/* ------------------------------------------------------------------------ */

struct waiter {
    struct sem** sems;
    int count;
    int64_t timeout;

    struct thread* volatile thr;
    volatile int done;
    volatile int result;
    volatile int selected;
    void* volatile wsc_after;    /* thr->waiting_sem_or_clot once woken */
    void* volatile nws_after;    /* thr->next_waiting_sem once woken */
};

struct worker {
    struct waiter* volatile job;
    volatile int busy;
    volatile int up;
};

static struct worker g_pool[POOL_SIZE];
static int g_pool_created;

static void RunWaiter(struct waiter* w) {
    struct thread* me = KeGetCurrentThread();
    int sel = -1;
    w->thr = me;
    int r = KeAcquireSemFromMany(w->sems, w->count, w->timeout, &sel);
    w->result = r;
    w->selected = sel;
    w->wsc_after = (void*) me->waiting_sem_or_clot;
    w->nws_after = (void*) me->next_waiting_sem;
    w->done = 1;                 /* last: the test thread may reuse *w now */
}

static void WorkerEntry(void* ctx) {
    KeBeginNewThread();
    struct worker* wk = ctx;
    wk->up = 1;
    for (;;) {                   /* never returns: no thread termination yet */
        struct waiter* w;
        while ((w = wk->job) == NULL) KeSchedule();
        wk->job = NULL;
        RunWaiter(w);
        wk->busy = 0;
    }
}

static void EnsurePool(void) {
    while (g_pool_created < POOL_SIZE) {
        struct worker* wk = &g_pool[g_pool_created++];
        wk->job = NULL; wk->busy = 0; wk->up = 0;
        KeCreateThread(KeGetKernelVas(), WorkerEntry, wk);
    }
    for (int i = 0; i < POOL_SIZE; ++i) SpinUntil(g_pool[i].up);
}

static struct worker* ClaimIdleWorker(void) {
    for (int i = 0; i < POOL_SIZE; ++i) {
        if (g_pool[i].up && !g_pool[i].busy) {
            g_pool[i].busy = 1;
            return &g_pool[i];
        }
    }
    return NULL;
}

/*
 * Starts a thread blocking on `sems` with `timeout` and returns once it is
 * visibly queued on all of them. Every sem must be held (count == max).
 */
static void Spawn(struct waiter* w, struct sem** sems, int count, int64_t timeout) {
    void* old_ends[8];
    CHECK(count >= 1 && count <= 8);

    memset((void*) w, 0, sizeof(*w));
    w->sems = sems;
    w->count = count;
    w->timeout = timeout;
    w->selected = -1;
    for (int i = 0; i < count; ++i) old_ends[i] = QueueEnd(sems[i]);

    EnsurePool();
    struct worker* wk;
    SpinUntil((wk = ClaimIdleWorker()) != NULL);
    wk->job = w;
    for (int i = 0; i < count; ++i) SpinUntil(QueueEnd(sems[i]) != old_ends[i]);
    Settle();
    CHECK(!w->done);
}

static void Spawn1(struct waiter* w, struct sem** slot, struct sem* s, int64_t timeout) {
    *slot = s;
    Spawn(w, slot, 1, timeout);
}

static struct sem* NewHeldSem(void) { return KeCreateSem(1, 1); }

/* ------------------------------------------------------------------------ */
/* Expectations                                                              */
/* ------------------------------------------------------------------------ */

static void ExpectTimedOut(struct waiter* w) {
    SpinUntil(w->done);
    CHECK(w->result == ETIMEDOUT);
    CHECK(w->wsc_after == NULL);
    CHECK(w->nws_after == NULL);
    SUSPECT(w->selected == -1, "selected_out was written on a timeout");
}

static void ExpectAcquired(struct waiter* w, int sel) {
    SpinUntil(w->done);
    CHECK(w->result == 0);
    CHECK(w->selected == sel);
    CHECK(w->wsc_after == NULL);
    CHECK(w->nws_after == NULL);
}

static void ExpectAbandoned(struct waiter* w) {
    SpinUntil(w->done);
    CHECK(w->result == EABANDONED);
    CHECK(w->wsc_after == NULL);
    CHECK(w->nws_after == NULL);
}

static void ExpectBlocked(struct waiter* w) {
    for (int i = 0; i < BLOCKED_PROBE; ++i) KeSchedule();
    CHECK(!w->done);
}

/* ------------------------------------------------------------------------ */
/* Without waiting                                                           */
/* ------------------------------------------------------------------------ */

/* A free sem is acquired at once, whatever the timeout. */
static void TestFreeSemIgnoresTimeout(void) {
    struct sem* s = KeCreateSem(3, 0);
    CHECK(KeAcquireSem(s, TIMEOUT_INSTANT) == 0);
    CHECK(KeAcquireSem(s, SEMTO(1000000)) == 0);
    CHECK(KeAcquireSem(s, TIMEOUT_INFINITE) == 0);
    CHECK(s->count == 3);
    CHECK(KeAcquireSem(s, TIMEOUT_INSTANT) == EAGAIN);
    CheckQueueEmpty(s);
    KeDerefObject(s);
}

static void TestInstantOnHeldIsEagain(void) {
    struct sem* s = NewHeldSem();
    for (int i = 0; i < 3; ++i) {
        CHECK(KeAcquireSem(s, TIMEOUT_INSTANT) == EAGAIN);
        CHECK(s->count == s->max);
        CheckQueueEmpty(s);
    }
    KeDerefObject(s);
}

/* Many-sem form: EAGAIN when all are held, 0 and the right index otherwise. */
static void TestInstantFromMany(void) {
    struct sem* s[2] = { KeCreateSem(1, 1), KeCreateSem(1, 1) };
    int sel = -1;
    CHECK(KeAcquireSemFromMany(s, 2, TIMEOUT_INSTANT, &sel) == EAGAIN);
    CHECK(sel == -1);
    CheckQueueEmpty(s[0]);
    CheckQueueEmpty(s[1]);

    CHECK(KeReleaseSem(s[1]) == 0);
    CHECK(KeAcquireSemFromMany(s, 2, TIMEOUT_INSTANT, &sel) == 0);
    CHECK(sel == 1);
    CHECK(KeAcquireSemFromMany(s, 2, TIMEOUT_INSTANT, &sel) == EAGAIN);
    KeDerefObject(s[0]);
    KeDerefObject(s[1]);
}

/* ------------------------------------------------------------------------ */
/* Single sem, one waiter                                                    */
/* ------------------------------------------------------------------------ */

/* Not a nanosecond early, and no later than the deadline. */
static void TestTimesOutAtDeadline(void) {
    struct sem* s = NewHeldSem(); struct sem* slot;
    struct waiter w;
    Spawn1(&w, &slot, s, SEMTO(1000));

    KeAdvanceTimer(999);
    ExpectBlocked(&w);

    KeAdvanceTimer(1);
    ExpectTimedOut(&w);
    CheckQueueEmpty(s);
    CHECK(s->count == s->max);          /* a timed-out waiter never held it */
    KeDerefObject(s);
}

/* Time jumps far past the deadline in one go. */
static void TestTimesOutAfterOvershoot(void) {
    struct sem* s = NewHeldSem(); struct sem* slot;
    struct waiter w;
    Spawn1(&w, &slot, s, SEMTO(1000));
    KeAdvanceTimer(5000000);
    ExpectTimedOut(&w);
    CheckQueueEmpty(s);
    KeDerefObject(s);
}

static void TestTimesOutAfterManySteps(void) {
    struct sem* s = NewHeldSem(); struct sem* slot;
    struct waiter w;
    Spawn1(&w, &slot, s, SEMTO(1000));
    for (int i = 0; i < 9; ++i) {
        KeAdvanceTimer(100);
        ExpectBlocked(&w);
    }
    KeAdvanceTimer(100);
    ExpectTimedOut(&w);
    KeDerefObject(s);
}

/* The shortest possible timeout. */
static void TestOneNanosecondTimeout(void) {
    struct sem* s = NewHeldSem(); struct sem* slot;
    struct waiter w;
    Spawn1(&w, &slot, s, SEMTO(1));
    KeAdvanceTimer(1);
    ExpectTimedOut(&w);
    KeDerefObject(s);
}

/* After a timeout the sem is a normal working sem, with no ghost waiter. */
static void TestSemUsableAfterTimeout(void) {
    struct sem* s = NewHeldSem(); struct sem* slot;
    struct waiter w;
    Spawn1(&w, &slot, s, SEMTO(100));
    KeAdvanceTimer(100);
    ExpectTimedOut(&w);

    CHECK(KeReleaseSem(s) == 0);                      /* nobody waiting: count-- */
    CHECK(s->count == s->max - 1);
    CHECK(KeAcquireSem(s, TIMEOUT_INSTANT) == 0);
    CHECK(s->count == s->max);
    CHECK(KeAcquireSem(s, TIMEOUT_INSTANT) == EAGAIN);
    KeDerefObject(s);
}

/* TIMEOUT_INFINITE must never time out, however far time moves. */
static void TestInfiniteNeverTimesOut(void) {
    struct sem* s = NewHeldSem(); struct sem* slot;
    struct waiter w;
    Spawn1(&w, &slot, s, TIMEOUT_INFINITE);
    KeAdvanceTimer(HUGE_NS);
    ExpectBlocked(&w);
    CHECK(KeReleaseSem(s) == 0);
    ExpectAcquired(&w, 0);
    KeDerefObject(s);
}

/* ------------------------------------------------------------------------ */
/* Timer and sem queues must clean up after each other                       */
/* ------------------------------------------------------------------------ */

/*
 * Woken by KeReleaseSem before the deadline. The same worker thread then waits
 * again with no timeout: a stale timer entry for it would wake it with
 * ETIMEDOUT as soon as the old deadline passed.
 */
static void TestReleaseBeforeTimeoutLeavesNoStaleTimer(void) {
    struct sem* s = NewHeldSem(); struct sem* slot;
    struct waiter w1, w2;

    Spawn1(&w1, &slot, s, SEMTO(10000));
    KeAdvanceTimer(5000);
    ExpectBlocked(&w1);
    CHECK(KeReleaseSem(s) == 0);
    ExpectAcquired(&w1, 0);
    CHECK(s->count == s->max);                      /* hand-off: still held */

    KeAdvanceTimer(HUGE_NS);                          /* old deadline long gone */

    struct thread* first = w1.thr;
    Spawn1(&w2, &slot, s, TIMEOUT_INFINITE);
    CHECK(w2.thr == first);                         /* same worker, as intended */
    KeAdvanceTimer(HUGE_NS);
    ExpectBlocked(&w2);
    CHECK(KeReleaseSem(s) == 0);
    ExpectAcquired(&w2, 0);
    KeDerefObject(s);
}

/* Same, but the second wait is also timed, so a stale entry corrupts the list. */
static void TestReleaseBeforeTimeoutThenTimedWait(void) {
    struct sem* s = NewHeldSem(); struct sem* slot;
    struct waiter w1, w2;

    Spawn1(&w1, &slot, s, SEMTO(10000));
    CHECK(KeReleaseSem(s) == 0);
    ExpectAcquired(&w1, 0);

    Spawn1(&w2, &slot, s, SEMTO(500));
    KeAdvanceTimer(499);
    ExpectBlocked(&w2);
    KeAdvanceTimer(1);
    ExpectTimedOut(&w2);
    CheckQueueEmpty(s);
    KeDerefObject(s);
}

/* The deadline passes, and KeReleaseSem runs before the waiter gets the CPU.
 * The waiter is already off the queue, so the release must not go to it. */
static void TestReleaseRightAfterTimeout(void) {
    struct sem* s = NewHeldSem(); struct sem* slot;
    struct waiter w;
    Spawn1(&w, &slot, s, SEMTO(1000));

    KeAdvanceTimer(1000);                             /* no yield since */
    CHECK(KeReleaseSem(s) == 0);
    CHECK(s->count == s->max - 1);                  /* count--, not a hand-off */
    ExpectTimedOut(&w);

    CHECK(KeAcquireSem(s, TIMEOUT_INSTANT) == 0);
    CHECK(s->count == s->max);
    KeDerefObject(s);
}

/* KeAbandonSem wakes a timed waiter with EABANDONED, and the timer lets go. */
static void TestAbandonTimedWaiter(void) {
    struct sem* s = NewHeldSem(); struct sem* slot;
    struct waiter w1, w2;

    Spawn1(&w1, &slot, s, SEMTO(5000));
    KeAbandonSem(s);
    ExpectAbandoned(&w1);
    CheckQueueEmpty(s);

    KeAdvanceTimer(HUGE_NS);

    struct thread* first = w1.thr;
    Spawn1(&w2, &slot, s, TIMEOUT_INFINITE);
    CHECK(w2.thr == first);
    KeAdvanceTimer(HUGE_NS);
    ExpectBlocked(&w2);
    KeAbandonSem(s);
    ExpectAbandoned(&w2);
    KeDerefObject(s);
}

/* Deadline reached, then KeAbandonSem before the waiter runs: the timeout wins. */
static void TestAbandonRightAfterTimeout(void) {
    struct sem* s = NewHeldSem(); struct sem* slot;
    struct waiter w;
    Spawn1(&w, &slot, s, SEMTO(1000));
    KeAdvanceTimer(1000);
    KeAbandonSem(s);
    ExpectTimedOut(&w);
    KeDerefObject(s);
}

/* The same worker times out several times in a row. */
static void TestRepeatedTimeoutsSameWorker(void) {
    struct sem* s = NewHeldSem(); struct sem* slot;
    struct thread* first = NULL;
    for (int round = 0; round < 4; ++round) {
        struct waiter w;
        Spawn1(&w, &slot, s, SEMTO(100 * (round + 1)));
        if (first == NULL) first = w.thr;
        CHECK(w.thr == first);
        KeAdvanceTimer(100 * (round + 1) - 1);
        ExpectBlocked(&w);
        KeAdvanceTimer(1);
        ExpectTimedOut(&w);
        CheckQueueEmpty(s);
    }
    struct waiter w;
    Spawn1(&w, &slot, s, SEMTO(100));
    CHECK(KeReleaseSem(s) == 0);
    ExpectAcquired(&w, 0);
    KeAdvanceTimer(HUGE_NS);
    KeDerefObject(s);
}

/* ------------------------------------------------------------------------ */
/* Several waiters on one sem                                                */
/* ------------------------------------------------------------------------ */

/*
 * Three waiters in FIFO order; the one at position `pos` has a timeout and the
 * others wait forever. After it times out the others must still be woken in
 * order, which fails if it was unlinked badly (head / middle / tail).
 */
static void TimeoutAtPosition(int pos) {
    struct sem* s = NewHeldSem();
    struct sem* slot[3];
    struct waiter w[3];

    for (int i = 0; i < 3; ++i) {
        Spawn1(&w[i], &slot[i], s, i == pos ? SEMTO(1000) : TIMEOUT_INFINITE);
    }

    KeAdvanceTimer(1000);
    ExpectTimedOut(&w[pos]);
    for (int i = 0; i < 3; ++i) {
        if (i != pos) ExpectBlocked(&w[i]);
    }

    int others[2], n = 0;
    for (int i = 0; i < 3; ++i) if (i != pos) others[n++] = i;

    CHECK(KeReleaseSem(s) == 0);
    ExpectAcquired(&w[others[0]], 0);
    ExpectBlocked(&w[others[1]]);
    CHECK(KeReleaseSem(s) == 0);
    ExpectAcquired(&w[others[1]], 0);

    CheckQueueEmpty(s);
    CHECK(s->count == s->max);
    KeDerefObject(s);
}

static void TestTimeoutOfHeadWaiter(void)   { TimeoutAtPosition(0); }
static void TestTimeoutOfMiddleWaiter(void) { TimeoutAtPosition(1); }
static void TestTimeoutOfTailWaiter(void)   { TimeoutAtPosition(2); }

/* A new waiter joins after the tail waiter timed out (queue end pointer). */
static void TestQueueEndAfterTailTimeout(void) {
    struct sem* s = NewHeldSem();
    struct sem* slot[3];
    struct waiter w[3];

    Spawn1(&w[0], &slot[0], s, TIMEOUT_INFINITE);
    Spawn1(&w[1], &slot[1], s, SEMTO(1000));
    KeAdvanceTimer(1000);
    ExpectTimedOut(&w[1]);

    Spawn1(&w[2], &slot[2], s, TIMEOUT_INFINITE);
    CHECK(KeReleaseSem(s) == 0);
    ExpectAcquired(&w[0], 0);
    ExpectBlocked(&w[2]);
    CHECK(KeReleaseSem(s) == 0);
    ExpectAcquired(&w[2], 0);
    CheckQueueEmpty(s);
    KeDerefObject(s);
}

/* Queue order and deadline order differ; each times out when its time comes. */
static void TestDeadlinesOutOfQueueOrder(void) {
    struct sem* s = NewHeldSem();
    struct sem* slot[3];
    struct waiter w[3];
    Spawn1(&w[0], &slot[0], s, SEMTO(3000));
    Spawn1(&w[1], &slot[1], s, SEMTO(1000));
    Spawn1(&w[2], &slot[2], s, SEMTO(2000));

    KeAdvanceTimer(1000);
    ExpectTimedOut(&w[1]);
    ExpectBlocked(&w[0]);
    ExpectBlocked(&w[2]);

    KeAdvanceTimer(1000);
    ExpectTimedOut(&w[2]);
    ExpectBlocked(&w[0]);

    KeAdvanceTimer(1000);
    ExpectTimedOut(&w[0]);
    CheckQueueEmpty(s);
    KeDerefObject(s);
}

static void TestSameDeadlineAllTimeOut(void) {
    struct sem* s = NewHeldSem();
    struct sem* slot[4];
    struct waiter w[4];
    for (int i = 0; i < 4; ++i) Spawn1(&w[i], &slot[i], s, SEMTO(500));
    KeAdvanceTimer(499);
    for (int i = 0; i < 4; ++i) CHECK(!w[i].done);
    KeAdvanceTimer(1);
    for (int i = 0; i < 4; ++i) ExpectTimedOut(&w[i]);
    CheckQueueEmpty(s);
    KeDerefObject(s);
}

/* One of two is released, the other (shorter deadline, behind it) times out. */
static void TestReleaseAndTimeoutMixed(void) {
    struct sem* s = NewHeldSem();
    struct sem* slot[2];
    struct waiter w[2];
    Spawn1(&w[0], &slot[0], s, SEMTO(9000));
    Spawn1(&w[1], &slot[1], s, SEMTO(1000));

    CHECK(KeReleaseSem(s) == 0);
    ExpectAcquired(&w[0], 0);
    ExpectBlocked(&w[1]);

    KeAdvanceTimer(1000);
    ExpectTimedOut(&w[1]);
    KeAdvanceTimer(HUGE_NS);                         /* w[0]'s old deadline: nothing */
    CheckQueueEmpty(s);
    KeDerefObject(s);
}

/* ------------------------------------------------------------------------ */
/* KeAcquireSemFromMany with a timeout                                         */
/* ------------------------------------------------------------------------ */

static void TestManyTimesOut(void) {
    struct sem* s[2] = { NewHeldSem(), NewHeldSem() };
    struct waiter w;
    Spawn(&w, s, 2, SEMTO(1000));

    KeAdvanceTimer(999);
    ExpectBlocked(&w);
    KeAdvanceTimer(1);
    ExpectTimedOut(&w);
    CheckQueueEmpty(s[0]);
    CheckQueueEmpty(s[1]);

    for (int i = 0; i < 2; ++i) {                   /* both still ordinary sems */
        CHECK(KeReleaseSem(s[i]) == 0);
        CHECK(s[i]->count == s[i]->max - 1);
        CHECK(KeAcquireSem(s[i], TIMEOUT_INSTANT) == 0);
        CHECK(KeAcquireSem(s[i], TIMEOUT_INSTANT) == EAGAIN);
    }
    KeDerefObject(s[0]);
    KeDerefObject(s[1]);
}

/* Won via one sem before the deadline; later time passing does nothing. */
static void TestManyReleasedBeforeTimeout(void) {
    struct sem* s[2] = { NewHeldSem(), NewHeldSem() };
    struct waiter w1, w2;
    Spawn(&w1, s, 2, SEMTO(5000));
    CHECK(KeReleaseSem(s[1]) == 0);
    ExpectAcquired(&w1, 1);
    CheckQueueEmpty(s[0]);
    CheckQueueEmpty(s[1]);

    KeAdvanceTimer(HUGE_NS);

    struct thread* first = w1.thr;
    Spawn(&w2, s, 2, TIMEOUT_INFINITE);
    CHECK(w2.thr == first);
    KeAdvanceTimer(HUGE_NS);
    ExpectBlocked(&w2);
    CHECK(KeReleaseSem(s[0]) == 0);
    ExpectAcquired(&w2, 0);
    KeDerefObject(s[0]);
    KeDerefObject(s[1]);
}

/* A timed clot in the middle of other waiters on both sems. */
static void TestManyTimeoutAmongOthers(void) {
    struct sem* A = NewHeldSem();
    struct sem* B = NewHeldSem();
    struct sem* both[2] = { A, B };
    struct sem* sa = A; struct sem* sb = B;
    struct sem* both2[2] = { A, B };
    struct waiter w[4];

    Spawn(&w[0], &sa, 1, TIMEOUT_INFINITE);         /* A: w0 */
    Spawn(&w[1], both, 2, SEMTO(1000));             /* A: w0 w1   B: w1 */
    Spawn(&w[2], &sb, 1, TIMEOUT_INFINITE);         /* B: w1 w2 */
    Spawn(&w[3], both2, 2, TIMEOUT_INFINITE);       /* A: w0 w1 w3   B: w1 w2 w3 */

    KeAdvanceTimer(1000);
    ExpectTimedOut(&w[1]);
    ExpectBlocked(&w[0]);
    ExpectBlocked(&w[2]);
    ExpectBlocked(&w[3]);

    CHECK(KeReleaseSem(A) == 0);
    ExpectAcquired(&w[0], 0);
    CHECK(KeReleaseSem(A) == 0);
    ExpectAcquired(&w[3], 0);                       /* clot: wins via A, leaves B */
    ExpectBlocked(&w[2]);
    CHECK(KeReleaseSem(B) == 0);
    ExpectAcquired(&w[2], 0);

    CheckQueueEmpty(A);
    CheckQueueEmpty(B);
    KeDerefObject(A);
    KeDerefObject(B);
}

/* ------------------------------------------------------------------------ */
/* Randomised: model of one sem with timed and untimed waiters               */
/* ------------------------------------------------------------------------ */

#define RW 6

struct mslot {
    struct waiter w;
    struct sem*   sems[1];
    int           active;
    int           finished;     /* model says it should be done */
    int           expect;       /* 0 or ETIMEDOUT once finished */
    uint64_t      deadline;     /* absolute; 0 = never */
};

static uint32_t g_rng;
static uint32_t Rand(void) {
    g_rng = g_rng * 1664525u + 1013904223u;
    return g_rng >> 8;
}

static void RandomisedRun(uint32_t seed, int steps) {
    struct sem* s = NewHeldSem();
    static struct mslot sl[RW];
    int order[RW];              /* model queue: slot indices, arrival order */
    int qn = 0;
    g_rng = seed;
    memset(sl, 0, sizeof(sl));

    for (int step = 0; step < steps; ++step) {
        int act = Rand() % 4;
        int nactive = 0;
        for (int i = 0; i < RW; ++i) nactive += sl[i].active;

        if ((act <= 1 && nactive < RW) || qn == 0) {
            /* Spawn into a free slot. */
            int i = 0;
            while (sl[i].active) ++i;
            if (nactive >= RW) continue;
            int64_t to;
            uint64_t dl = 0;
            if (Rand() % 3 == 0) {
                to = TIMEOUT_INFINITE;
            } else {
                uint64_t ns = 1 + Rand() % 2000;
                to = SEMTO(ns);
                dl = KeGetTimeSinceBoot() + ns;
            }
            Spawn1(&sl[i].w, &sl[i].sems[0], s, to);
            sl[i].active = 1;
            sl[i].finished = 0;
            sl[i].deadline = dl;
            order[qn++] = i;
        } else if (act == 2) {
            /* Advance time; everything with a due deadline times out. */
            uint64_t dt = Rand() % 700;
            KeAdvanceTimer(dt);
            uint64_t now = KeGetTimeSinceBoot();
            int keep = 0;
            for (int k = 0; k < qn; ++k) {
                struct mslot* m = &sl[order[k]];
                if (m->deadline != 0 && m->deadline <= now) {
                    m->finished = 1;
                    m->expect = ETIMEDOUT;
                } else {
                    order[keep++] = order[k];
                }
            }
            qn = keep;
        } else {
            /* Release: the head of the queue gets it. */
            CHECK(qn > 0);
            CHECK(KeReleaseSem(s) == 0);
            struct mslot* m = &sl[order[0]];
            m->finished = 1;
            m->expect = 0;
            for (int k = 1; k < qn; ++k) order[k - 1] = order[k];
            --qn;
        }

        /* Everyone the model says is finished must finish; the rest must not. */
        Settle();
        for (int i = 0; i < RW; ++i) {
            if (!sl[i].active) continue;
            if (sl[i].finished) {
                SpinUntil(sl[i].w.done);
                CHECK(sl[i].w.result == sl[i].expect);
                CHECK(sl[i].w.wsc_after == NULL && sl[i].w.nws_after == NULL);
                sl[i].active = 0;
            } else {
                CHECK(!sl[i].w.done);
            }
        }
        CHECK(s->count == s->max);
        CHECK((QueueStart(s) == NULL) == (qn == 0));
    }

    /* Drain whatever is left. */
    while (qn > 0) {
        CHECK(KeReleaseSem(s) == 0);
        struct mslot* m = &sl[order[0]];
        for (int k = 1; k < qn; ++k) order[k - 1] = order[k];
        --qn;
        SpinUntil(m->w.done);
        CHECK(m->w.result == 0);
        m->active = 0;
    }
    CheckQueueEmpty(s);
    KeAdvanceTimer(HUGE_NS);          /* nothing may be left on the timer list */
    Settle();
    KeDerefObject(s);
}

static void TestRandomised(void) {
    for (uint32_t seed = 1; seed <= 4; ++seed) {
        KeLogPrintf("  randomised: seed %d\n", (int) seed);
        RandomisedRun(seed * 7919u, 40);
    }
}

/* ------------------------------------------------------------------------ */

void RunSemTimeoutTests(void) {
    EnsurePool();
    g_suspected = 0;

    ArchDebugInhibitTimerAdvance(true);

    RUN_TEST(TestFreeSemIgnoresTimeout);
    RUN_TEST(TestInstantOnHeldIsEagain);
    RUN_TEST(TestInstantFromMany);

    RUN_TEST(TestTimesOutAtDeadline);
    RUN_TEST(TestTimesOutAfterOvershoot);
    RUN_TEST(TestTimesOutAfterManySteps);
    RUN_TEST(TestOneNanosecondTimeout);
    RUN_TEST(TestSemUsableAfterTimeout);
    RUN_TEST(TestInfiniteNeverTimesOut);

    RUN_TEST(TestReleaseBeforeTimeoutLeavesNoStaleTimer);
    RUN_TEST(TestReleaseBeforeTimeoutThenTimedWait);
    RUN_TEST(TestReleaseRightAfterTimeout);
    RUN_TEST(TestAbandonTimedWaiter);
    RUN_TEST(TestAbandonRightAfterTimeout);
    RUN_TEST(TestRepeatedTimeoutsSameWorker);

    RUN_TEST(TestTimeoutOfHeadWaiter);
    RUN_TEST(TestTimeoutOfMiddleWaiter);
    RUN_TEST(TestTimeoutOfTailWaiter);
    RUN_TEST(TestQueueEndAfterTailTimeout);
    RUN_TEST(TestDeadlinesOutOfQueueOrder);
    RUN_TEST(TestSameDeadlineAllTimeOut);
    RUN_TEST(TestReleaseAndTimeoutMixed);

    RUN_TEST(TestManyTimesOut);
    RUN_TEST(TestManyReleasedBeforeTimeout);
    RUN_TEST(TestManyTimeoutAmongOthers);

    RUN_TEST(TestRandomised);

    ArchDebugInhibitTimerAdvance(false);

    if (g_suspected) KeLogPrintf("sem timeout tests: %d SUSPECTED BUG(S)\n", g_suspected);
    else             KeLogPrintf("sem timeout tests: ALL PASSED\n");
}