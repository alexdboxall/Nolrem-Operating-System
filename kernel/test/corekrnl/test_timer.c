/*
 * Timer tests.
 */

#include <common.h>
#include <scheduler.h>
#include <spinlock.h>
#include <arch.h>
#include <log.h>
#include <thread.h>
#include <vmm.h>
#include <timer.h>
#include <_test.h>

#define NWORKERS     4
#define SPIN_LIMIT   2000
#define SETTLE       3      /* KeSchedule() passes to let woken threads run */

enum { MODE_NANO = 1, MODE_UNTIL, MODE_MILLI };

struct tworker {
    volatile int      job;       /* 0 = idle, 1 = has a job */
    volatile int      mode;
    volatile uint64_t arg;
    volatile int      started;   /* set just before the Sleep call */
    volatile int      done;      /* set just after it returns */
    volatile int      wake_seq;  /* 1-based order in which workers woke */
};

static struct tworker   g_w[NWORKERS];
static volatile int     g_claimed;
static volatile int     g_created;
static volatile int     g_wake_counter;
static volatile int     g_suspected;

#define SUSPECT(cond, what) do { \
    if (!(cond)) { g_suspected++; KeLogPrintf("SUSPECTED BUG: %s\n", what); assert(false); } \
} while (0)

static void Settle(void) {
    for (int i = 0; i < SETTLE; ++i) KeSchedule();
}

static void WorkerEntry(void* ctx) {
    KeBeginNewThread();
    struct tworker* w = ctx;
    for (;;) {
        while (!w->job) KeSchedule();
        w->started = 1;
        switch (w->mode) {
            case MODE_NANO:  KeSleepNano(w->arg);  break;
            case MODE_UNTIL: KeSleepUntil(w->arg); break;
            case MODE_MILLI: KeSleepMilli(w->arg); break;
        }
        w->wake_seq = ++g_wake_counter;
        w->done = 1;
        w->job = 0;
    }
}

static void EnsureWorkers(void) {
    while (g_created < NWORKERS) {
        KeCreateThread(KeGetKernelVas(), WorkerEntry, &g_w[g_created]);
        g_created++;
    }
}

/* Give worker i a job and wait until it is asleep. */
static void StartSleep(int i, int mode, uint64_t arg) {
    struct tworker* w = &g_w[i];
    CHECK(w->job == 0);
    w->mode = mode;
    w->arg = arg;
    w->started = 0;
    w->done = 0;
    w->wake_seq = 0;
    w->job = 1;
    for (int n = 0; !w->started; ++n) {
        CHECK(n < SPIN_LIMIT);
        KeSchedule();
    }
    Settle();
}

static int IsDone(int i) { return g_w[i].done; }

/* Wait for worker i to wake (after an KeAdvanceTimer that should wake it). */
static int WaitDone(int i) {
    for (int n = 0; n < SPIN_LIMIT; ++n) {
        if (g_w[i].done) return 1;
        KeSchedule();
    }
    return 0;
}

/* Leave nothing asleep: used between tests. */
static void FlushAll(void) {
    KeAdvanceTimer(1000ULL * 1000 * 1000 * 1000);
    Settle();
    for (int i = 0; i < NWORKERS; ++i) {
        if (g_w[i].job) CHECK(WaitDone(i));
    }
}

/* ------------------------------------------------------------------------ */

static void TestTimeAdvances(void) {
    uint64_t t0 = KeGetTimeSinceBoot();
    KeAdvanceTimer(1000);
    CHECK(KeGetTimeSinceBoot() == t0 + 1000);
    KeAdvanceTimer(0);
    CHECK(KeGetTimeSinceBoot() == t0 + 1000);
    KeAdvanceTimer(1);
    CHECK(KeGetTimeSinceBoot() == t0 + 1001);
}

/* Wakes at the deadline, and not a nanosecond before. */
static void TestSleepNanoExactDeadline(void) {
    StartSleep(0, MODE_NANO, 1000);
    CHECK(!IsDone(0));

    KeAdvanceTimer(999);
    Settle();
    CHECK(!IsDone(0));          /* 1ns early */

    KeAdvanceTimer(1);
    CHECK(WaitDone(0));
}

/* Time jumps well past the deadline (the usual real-world case). */
static void TestSleepNanoOvershoot(void) {
    StartSleep(0, MODE_NANO, 1000);
    KeAdvanceTimer(5000000);
    CHECK(WaitDone(0));
}

/* Many small steps add up. */
static void TestSleepNanoManySteps(void) {
    StartSleep(0, MODE_NANO, 1000);
    for (int i = 0; i < 9; ++i) {
        KeAdvanceTimer(100);
        Settle();
        CHECK(!IsDone(0));
    }
    KeAdvanceTimer(100);
    CHECK(WaitDone(0));
}

static void TestSleepUntilAbsolute(void) {
    uint64_t target = KeGetTimeSinceBoot() + 5000;
    StartSleep(0, MODE_UNTIL, target);

    KeAdvanceTimer(4999);
    Settle();
    CHECK(!IsDone(0));

    KeAdvanceTimer(1);
    CHECK(WaitDone(0));
    CHECK(KeGetTimeSinceBoot() == target);
}

/* KeSleepMilli(n) must sleep n milliseconds = n * 1,000,000 ns. */
static void TestSleepMilliScale(void) {
    StartSleep(0, MODE_MILLI, 2);

    KeAdvanceTimer(1999999);
    Settle();
    CHECK(!IsDone(0));

    KeAdvanceTimer(1);
    CHECK(WaitDone(0));
}

/* Sleepers with different deadlines wake in deadline order, and a sleeper
 * only wakes once its own deadline is reached. The longest sleeper goes to
 * sleep first, so the later ones must be inserted ahead of it. */
static void TestWakeOrder(void) {
    StartSleep(0, MODE_NANO, 3000);
    StartSleep(1, MODE_NANO, 1000);     /* earlier than the head of the queue */
    StartSleep(2, MODE_NANO, 2000);     /* between the other two */

    KeAdvanceTimer(500);
    Settle();
    CHECK(!IsDone(0) && !IsDone(1) && !IsDone(2));

    KeAdvanceTimer(500);                  /* t = 1000 */
    CHECK(WaitDone(1));
    Settle();
    CHECK(!IsDone(0) && !IsDone(2));

    KeAdvanceTimer(1000);                 /* t = 2000 */
    CHECK(WaitDone(2));
    Settle();
    CHECK(!IsDone(0));

    KeAdvanceTimer(1000);                 /* t = 3000 */
    CHECK(WaitDone(0));

    CHECK(g_w[1].wake_seq == 1);
    CHECK(g_w[2].wake_seq == 2);
    CHECK(g_w[0].wake_seq == 3);
}

/* Same thing but sleeping in ascending order, and one big jump at the end. */
static void TestWakeOrderAscendingOneJump(void) {
    g_wake_counter = 0;
    StartSleep(0, MODE_NANO, 1000);
    StartSleep(1, MODE_NANO, 2000);
    StartSleep(2, MODE_NANO, 3000);

    KeAdvanceTimer(10000);
    CHECK(WaitDone(0) && WaitDone(1) && WaitDone(2));
    /* all three are runnable at once, so only check nobody was missed */
    CHECK(g_w[0].wake_seq && g_w[1].wake_seq && g_w[2].wake_seq);
}

/* Two sleepers with the same deadline both wake from a single advance. */
static void TestSameDeadline(void) {
    StartSleep(0, MODE_NANO, 1000);
    StartSleep(1, MODE_NANO, 1000);
    KeAdvanceTimer(1000);
    CHECK(WaitDone(0));
    CHECK(WaitDone(1));
}

/* After a queue empties it works again, and the same thread can sleep twice. */
static void TestSleepTwice(void) {
    StartSleep(0, MODE_NANO, 1000);
    KeAdvanceTimer(1000);
    CHECK(WaitDone(0));

    StartSleep(0, MODE_NANO, 2000);
    KeAdvanceTimer(1999);
    Settle();
    CHECK(!IsDone(0));
    KeAdvanceTimer(1);
    CHECK(WaitDone(0));

    /* queue is empty again; a time jump with nobody asleep must be harmless */
    KeAdvanceTimer(123456);
    Settle();
}

/* A sleeper added later with a *later* deadline must not disturb an earlier
 * one that is already waiting. */
static void TestLaterSleeperDoesntDelayEarlier(void) {
    StartSleep(0, MODE_NANO, 1000);
    StartSleep(1, MODE_NANO, 9000);
    KeAdvanceTimer(1000);
    CHECK(WaitDone(0));
    Settle();
    CHECK(!IsDone(1));
    KeAdvanceTimer(8000);
    CHECK(WaitDone(1));
}

/* Soft checks: a deadline that has already passed shouldn't sleep forever. */
static void TestSleepAlreadyElapsed(void) {
    struct tworker* w = &g_w[0];
    w->mode = MODE_NANO; w->arg = 0;
    w->started = 0; w->done = 0; w->job = 1;
    for (int n = 0; !w->started; ++n) { CHECK(n < SPIN_LIMIT); KeSchedule(); }
    Settle();
    SUSPECT(w->done, "KeSleepNano(0) did not return without a timer tick");

    w->done = 0;
    w->mode = MODE_UNTIL; w->arg = 1;       /* long in the past */
    if (w->job == 0) {
        w->started = 0; w->job = 1;
        for (int n = 0; !w->started; ++n) { CHECK(n < SPIN_LIMIT); KeSchedule(); }
        Settle();
        SUSPECT(w->done, "KeSleepUntil(<past>) did not return without a timer tick");
    }
}

void RunTimerTests(void) {
    EnsureWorkers();
    g_suspected = 0;

    ArchDebugInhibitTimerAdvance(true);

    RUN_TEST(TestTimeAdvances);
    RUN_TEST(TestSleepNanoExactDeadline);   FlushAll();
    RUN_TEST(TestSleepNanoOvershoot);       FlushAll();
    RUN_TEST(TestSleepNanoManySteps);       FlushAll();
    RUN_TEST(TestSleepUntilAbsolute);       FlushAll();
    RUN_TEST(TestSleepMilliScale);          FlushAll();
    g_wake_counter = 0;
    RUN_TEST(TestWakeOrder);                FlushAll();
    RUN_TEST(TestWakeOrderAscendingOneJump);FlushAll();
    RUN_TEST(TestSameDeadline);             FlushAll();
    RUN_TEST(TestSleepTwice);               FlushAll();
    RUN_TEST(TestLaterSleeperDoesntDelayEarlier); FlushAll();
    RUN_TEST(TestSleepAlreadyElapsed);      FlushAll();

    ArchDebugInhibitTimerAdvance(false);

    if (g_suspected) KeLogPrintf("timer tests: %d SUSPECTED BUG(S)\n", g_suspected);
    else             KeLogPrintf("timer tests: ALL PASSED\n");
}