/*
 * Message box test suite.
 *
 * Call RunMsgboxTests() from a kernel thread once scheduling is up. A failing
 * CHECK() halts (assert), so reaching the end means everything passed, except
 * for the SUSPECTED BUG lines: those tests use soft checks that log and carry
 * on, so one run reports all of them. They are grouped last.
 *
 * Only the public API is used (the struct is private to msgbox.c), so these
 * are black-box tests. Finite timeouts are not tested because they aren't
 * implemented yet; only TIMEOUT_INSTANT and TIMEOUT_INFINITE are used.
 *
 * ASSUMPTIONS
 *   - Cooperative scheduling: a thread runs until it blocks or calls
 *     Schedule(). A few tests rely on that, e.g. "once a job reports it has
 *     started, it is already blocked or finished", and the stress tests'
 *     dequeue log. If preemption is ever turned on, those need revisiting.
 *   - Threads can't terminate yet, so a small pool of workers is created on
 *     demand and reused. Workers never return from their entry function.
 *   - CreateThread's context argument works (it didn't before the stack fix).
 *     The worker checks its argument and asserts if it is wrong.
 */

#include <common.h>
#include <scheduler.h>
#include <spinlock.h>
#include <arch.h>
#include <log.h>
#include <thread.h>
#include <obj.h>
#include <sem.h>
#include <vmm.h>
#include <msgbox.h>
#include <_test.h>
#include <heap.h>

#ifndef MSGTEST_YIELD
#define MSGTEST_YIELD() Schedule()
#endif
#define YIELD() MSGTEST_YIELD()

/* One Schedule() walks every runnable thread once, so a "spin" is a full pass. */
#define SPIN_WARN        200
#define SPIN_LIMIT       3000
#define SPIN_LIMIT_LONG  30000
#define SETTLE_YIELDS    3

#define SZ        8      /* default message size */
#define MSG_MAX   32     /* largest message size used by any test */
#define GUARD     8
#define POOL_SIZE 12
#define MAX_JOBS  16     /* jobs in use at once; the table is reused by every test */

/* ------------------------------------------------------------------------ */
/* Waiting                                                                   */
/* ------------------------------------------------------------------------ */

/* `cond` is evaluated exactly once per iteration. ok_out = 1 if it became true. */
#define WAIT_RESULT_N(cond, ok_out, limit)                                   \
    do {                                                                     \
        int _spins = 0;                                                      \
        (ok_out) = 0;                                                        \
        for (;;) {                                                           \
            if (cond) {                                                      \
                (ok_out) = 1;                                                \
                break;                                                       \
            }                                                                \
            if (_spins == 0) {                                               \
                TRACE("  wait: %s\n", #cond);                                \
            }                                                                \
            YIELD();                                                         \
            if (++_spins == SPIN_WARN) {                                     \
                TRACE("msgbox test: STILL WAITING for: %s\n", #cond);        \
            }                                                                \
            if (_spins > (limit)) {                                          \
                break;                                                       \
            }                                                                \
        }                                                                    \
    } while (0)

#define WAIT_FOR_N(cond, limit)                                              \
    do {                                                                     \
        int _wait_ok;                                                        \
        WAIT_RESULT_N(cond, _wait_ok, limit);                                \
        if (!_wait_ok) {                                                     \
            TRACE("msgbox test: TIMED OUT waiting for: %s\n", #cond);        \
        }                                                                    \
        CHECK(_wait_ok);                                                     \
    } while (0)

#define WAIT_FOR(cond) WAIT_FOR_N(cond, SPIN_LIMIT)

/* Soft check for the suspected-bug group: log and carry on. */
static int g_suspected;
#define SUSPECT(cond, what)                                                  \
    do {                                                                     \
        if (!(cond)) {                                                       \
            TRACE("SUSPECTED BUG: %s\n", what);                              \
            ++g_suspected;                                                   \
        }                                                                    \
    } while (0)

static void Settle(void) {
    for (int i = 0; i < SETTLE_YIELDS; ++i) {
        YIELD();
    }
}

/* ------------------------------------------------------------------------ */
/* Message contents                                                          */
/* ------------------------------------------------------------------------ */

/* Deterministic content for a seed. Byte 0 is the seed itself, so seeds < 256
 * stay distinct even for 1-byte messages. */
static void FillMsg(uint8_t* buf, size_t size, uint32_t seed) {
    for (size_t i = 0; i < size; ++i) {
        if (i == 0) {
            buf[0] = (uint8_t) seed;
            continue;
        }
        uint32_t h = seed * 2654435761u ^ (uint32_t) (i + 1) * 40503u;
        h ^= h >> 15;
        h *= 2246822519u;
        h ^= h >> 13;
        buf[i] = (uint8_t) h;
    }
}

static bool MsgMatches(const uint8_t* buf, size_t size, uint32_t seed) {
    uint8_t expect[MSG_MAX];
    FillMsg(expect, size, seed);
    return memcmp(buf, expect, size) == 0;
}

/* A receive buffer surrounded by guard bytes, to catch over- and under-runs. */
struct gbuf {
    uint8_t raw[GUARD + MSG_MAX + GUARD];
};

static void GbufInit(struct gbuf* g) {
    memset(g->raw, 0xA5, sizeof(g->raw));
}

static uint8_t* GbufData(struct gbuf* g) {
    return g->raw + GUARD;
}

/* Everything outside the first `size` data bytes must be untouched. */
static void GbufCheckGuards(struct gbuf* g, size_t size) {
    for (int i = 0; i < GUARD; ++i) {
        CHECK(g->raw[i] == 0xA5);
    }
    for (size_t i = GUARD + size; i < sizeof(g->raw); ++i) {
        CHECK(g->raw[i] == 0xA5);
    }
}

static void GbufCheckUntouched(struct gbuf* g) {
    for (size_t i = 0; i < sizeof(g->raw); ++i) {
        CHECK(g->raw[i] == 0xA5);
    }
}

/* ------------------------------------------------------------------------ */
/* Non-blocking helpers for the test thread                                  */
/* ------------------------------------------------------------------------ */

static int PostSeed(struct msgbox* box, size_t size, uint32_t seed, int64_t timeout) {
    uint8_t m[MSG_MAX];
    FillMsg(m, size, seed);
    return KePostMessage(box, m, timeout);
}

static int Replace(struct msgbox* box, size_t size, uint32_t cmp_seed, uint32_t new_seed, int64_t timeout) {
    uint8_t c[MSG_MAX];
    uint8_t n[MSG_MAX];
    FillMsg(c, size, cmp_seed);
    FillMsg(n, size, new_seed);
    return KeTryReplaceOrAdd(box, c, n, timeout);
}

static void ExpectPost(struct msgbox* box, size_t size, uint32_t seed) {
    CHECK(PostSeed(box, size, seed, TIMEOUT_INSTANT) == 0);
}

/* The box is full: a post must be refused, and must not be stored. */
static void ExpectFull(struct msgbox* box, size_t size) {
    CHECK(PostSeed(box, size, 0xF00D, TIMEOUT_INSTANT) == EAGAIN);
}

static void ExpectGet(struct msgbox* box, size_t size, uint32_t seed) {
    struct gbuf g;
    GbufInit(&g);
    CHECK(KeGetMessage(box, GbufData(&g), TIMEOUT_INSTANT) == 0);
    CHECK(MsgMatches(GbufData(&g), size, seed));
    GbufCheckGuards(&g, size);
}

static void ExpectPeek(struct msgbox* box, size_t size, uint32_t seed) {
    struct gbuf g;
    GbufInit(&g);
    CHECK(KePeekMessage(box, GbufData(&g), TIMEOUT_INSTANT) == 0);
    CHECK(MsgMatches(GbufData(&g), size, seed));
    GbufCheckGuards(&g, size);
}

/* Nothing to read, and a failed read must not write to the buffer. */
static void ExpectEmpty(struct msgbox* box, size_t size) {
    (void) size;
    struct gbuf g;
    GbufInit(&g);
    CHECK(KeGetMessage(box, GbufData(&g), TIMEOUT_INSTANT) == EAGAIN);
    GbufCheckUntouched(&g);
    CHECK(KePeekMessage(box, GbufData(&g), TIMEOUT_INSTANT) == EAGAIN);
    GbufCheckUntouched(&g);
}

/* Retry loops for the test thread, so it never blocks in the kernel itself. */
static void GetWait(struct msgbox* box, uint8_t* out) {
    int spins = 0;
    for (;;) {
        int r = KeGetMessage(box, out, TIMEOUT_INSTANT);
        if (r == 0) {
            return;
        }
        CHECK(r == EAGAIN);
        if (++spins > SPIN_LIMIT) {
            TRACE("msgbox test: TIMED OUT in GetWait\n");
            CHECK(0);
        }
        YIELD();
    }
}

static void PostWait(struct msgbox* box, size_t size, uint32_t seed) {
    int spins = 0;
    for (;;) {
        int r = PostSeed(box, size, seed, TIMEOUT_INSTANT);
        if (r == 0) {
            return;
        }
        CHECK(r == EAGAIN);
        if (++spins > SPIN_LIMIT) {
            TRACE("msgbox test: TIMED OUT in PostWait\n");
            CHECK(0);
        }
        YIELD();
    }
}

static struct msgbox* NewBox(size_t size, size_t cap) {
    struct msgbox* box = CreateMessageBox(size, cap);
    CHECK(box != NULL);
    return box;
}

/* ------------------------------------------------------------------------ */
/* Worker pool and jobs                                                      */
/* ------------------------------------------------------------------------ */

enum jobkind {
    JOB_GET,
    JOB_PEEK,
    JOB_POST,
    JOB_REPLACE,
    JOB_GET_MANY,
    JOB_SEQ_PRODUCE,   /* post `count` messages with seeds base, base+1, ... */
    JOB_SEQ_CONSUME,   /* get `count` messages and check they are base, base+1, ... */
    JOB_PC_PRODUCE,    /* stress producer, see below */
    JOB_PC_CONSUME     /* stress consumer, see below */
};

struct tjob {
    int kind;

    /* Inputs. */
    struct msgbox* box;
    struct msgbox** boxes;
    int nboxes;
    bool remove;
    int64_t timeout;
    size_t size;
    uint8_t in[MSG_MAX];
    uint8_t cmp[MSG_MAX];
    int producer_id;
    int count;
    uint32_t base;

    /* Outputs. `started` is set right before the (possibly blocking) call, so
     * once the test thread sees it, the job is already blocked or finished. */
    volatile int started;
    volatile int done;
    volatile int result;
    volatile int box_out;
    volatile int progress;
    uint8_t out[MSG_MAX];
};

/* Stress state, shared by the producer and consumer jobs. */
struct pc_msg {
    uint32_t producer;
    uint32_t box;
    uint32_t seq;
};

struct pc_log {
    uint32_t producer;
    uint32_t box;
    uint32_t seq;
    int box_out;
};

#define MAX_LOG 96       /* largest stress run delivers 75 messages */
static struct pc_log g_log[MAX_LOG];
static volatile int g_log_n;
static volatile int g_tickets;

/* Job slots are handed out by NewJob and all given back when a test starts (RUN
 * below resets g_njobs), so MAX_JOBS only has to cover the busiest single test
 * (the stress runs: 6). A job whose worker is still stuck from an earlier test
 * never touches its slot again, so reusing the slot is safe. */
static struct tjob g_jobs[MAX_JOBS];
static int g_njobs;

static struct tjob* NewJob(int kind) {
    CHECK(g_njobs < MAX_JOBS);
    struct tjob* j = &g_jobs[g_njobs++];
    memset((void*) j, 0, sizeof(*j));
    j->kind = kind;
    j->timeout = TIMEOUT_INFINITE;
    j->size = SZ;
    j->box_out = -1;
    return j;
}

static void RunJob(struct tjob* j) {
    j->started = 1;
    switch (j->kind) {
    case JOB_GET:
        j->result = KeGetMessage(j->box, j->out, j->timeout);
        break;

    case JOB_PEEK:
        j->result = KePeekMessage(j->box, j->out, j->timeout);
        break;

    case JOB_POST:
        j->result = KePostMessage(j->box, j->in, j->timeout);
        break;

    case JOB_REPLACE:
        j->result = KeTryReplaceOrAdd(j->box, j->cmp, j->in, j->timeout);
        break;

    case JOB_GET_MANY: {
        int bo = -1;
        int r = KeGetMessageFromMany(j->boxes, j->nboxes, j->out, j->timeout, j->remove, &bo);
        j->box_out = bo;
        j->result = r;
        break;
    }

    case JOB_SEQ_PRODUCE: {
        int r = 0;
        for (int i = 0; i < j->count && r == 0; ++i) {
            FillMsg(j->in, j->size, j->base + (uint32_t) i);
            r = KePostMessage(j->box, j->in, TIMEOUT_INFINITE);
            if (r == 0) {
                ++j->progress;
            }
        }
        j->result = r;
        break;
    }

    case JOB_SEQ_CONSUME: {
        int r = 0;
        for (int i = 0; i < j->count && r == 0; ++i) {
            r = KeGetMessage(j->box, j->out, TIMEOUT_INFINITE);
            if (r == 0 && !MsgMatches(j->out, j->size, j->base + (uint32_t) i)) {
                r = -1000 - i;      /* wrong message at position i */
            }
            if (r == 0) {
                ++j->progress;
            }
        }
        j->result = r;
        break;
    }

    case JOB_PC_PRODUCE: {
        int r = 0;
        for (int seq = 0; seq < j->count && r == 0; ++seq) {
            struct pc_msg m;
            m.producer = (uint32_t) j->producer_id;
            m.box = (uint32_t) ((seq * 7 + j->producer_id) % j->nboxes);
            m.seq = (uint32_t) seq;
            r = KePostMessage(j->boxes[m.box], &m, TIMEOUT_INFINITE);
            if (r == 0) {
                ++j->progress;
            }
        }
        j->result = r;
        break;
    }

    case JOB_PC_CONSUME: {
        int r = 0;
        /* Take a ticket before blocking, so exactly one Get happens per message
         * that will ever arrive and no consumer is left blocked at the end. */
        while (r == 0 && g_tickets > 0) {
            --g_tickets;
            struct pc_msg m;
            int bo = -1;
            r = KeGetMessageFromMany(j->boxes, j->nboxes, &m, TIMEOUT_INFINITE, true, &bo);
            if (r == 0) {
                CHECK(g_log_n < MAX_LOG);
                struct pc_log* e = &g_log[g_log_n++];
                e->producer = m.producer;
                e->box = m.box;
                e->seq = m.seq;
                e->box_out = bo;
                ++j->progress;
            }
        }
        j->result = r;
        break;
    }
    }
    j->done = 1;      /* last: the test thread may move on as soon as this is set */
}

struct tworker {
    struct tjob* volatile job;
    volatile int busy;
    volatile int started;
};

static struct tworker g_pool[POOL_SIZE];
static int g_pool_created;

/* Workers never return: there is no thread termination yet. */
static void WorkerEntry(void* ctx) {
    BeginNewThread();

    struct tworker* wk = ctx;
    CHECK(wk >= g_pool && wk < g_pool + POOL_SIZE);   /* the context argument arrived intact */
    wk->started = 1;

    for (;;) {
        struct tjob* j;
        while ((j = wk->job) == NULL) {
            YIELD();
        }
        wk->job = NULL;
        RunJob(j);
        wk->busy = 0;
    }
}

/* Reuses an idle worker, or creates one (workers are made on demand to keep the
 * number of threads in each Schedule() pass, and so the test time, down). */
static struct tworker* ClaimWorker(void) {
    for (int i = 0; i < g_pool_created; ++i) {
        if (g_pool[i].started && !g_pool[i].busy) {
            g_pool[i].busy = 1;
            return &g_pool[i];
        }
    }
    if (g_pool_created >= POOL_SIZE) {
        TRACE("msgbox test: out of workers (a previous job is stuck, or too many at once)\n");
        CHECK(0);
    }
    int i = g_pool_created++;
    g_pool[i].job = NULL;
    g_pool[i].busy = 1;
    g_pool[i].started = 0;
    CreateThread(GetKernelVas(), WorkerEntry, &g_pool[i]);
    WAIT_FOR(g_pool[i].started);
    return &g_pool[i];
}

/* Runs the job on a worker. Returns once the job has begun its call. */
static void Start(struct tjob* j) {
    struct tworker* w = ClaimWorker();
    w->job = j;
    WAIT_FOR(j->started);
}

static void ExpectBlocked(struct tjob* j) {
    Settle();
    CHECK(!j->done);
}

static void WaitDone(struct tjob* j) {
    WAIT_FOR(j->done);
}

static void ExpectJobMsg(struct tjob* j, size_t size, uint32_t seed) {
    WaitDone(j);
    CHECK(j->result == 0);
    CHECK(MsgMatches(j->out, size, seed));
}

static struct tjob* JobGet(struct msgbox* box) {
    struct tjob* j = NewJob(JOB_GET);
    j->box = box;
    return j;
}

static struct tjob* JobPeek(struct msgbox* box) {
    struct tjob* j = NewJob(JOB_PEEK);
    j->box = box;
    return j;
}

static struct tjob* JobPost(struct msgbox* box, uint32_t seed) {
    struct tjob* j = NewJob(JOB_POST);
    j->box = box;
    FillMsg(j->in, j->size, seed);
    return j;
}

static struct tjob* JobReplace(struct msgbox* box, uint32_t cmp_seed, uint32_t new_seed) {
    struct tjob* j = NewJob(JOB_REPLACE);
    j->box = box;
    FillMsg(j->cmp, j->size, cmp_seed);
    FillMsg(j->in, j->size, new_seed);
    return j;
}

static struct tjob* JobGetMany(struct msgbox** boxes, int n, bool remove) {
    struct tjob* j = NewJob(JOB_GET_MANY);
    j->boxes = boxes;
    j->nboxes = n;
    j->remove = remove;
    return j;
}

/* ------------------------------------------------------------------------ */
/* Basics                                                                    */
/* ------------------------------------------------------------------------ */

static void TestCreateDestroy(void) {
    struct msgbox* box = NewBox(SZ, 4);
    ExpectEmpty(box, SZ);
    DerefObject(box);
}

static void TestPostGetSingle(void) {
    struct msgbox* box = NewBox(SZ, 4);
    ExpectPost(box, SZ, 1);
    ExpectGet(box, SZ, 1);
    ExpectEmpty(box, SZ);
    DerefObject(box);
}

/* Exact bytes in and out, for sizes that aren't word-aligned, with the source
 * left alone and nothing written past the destination. */
static void TestMessageSizes(void) {
    static const size_t sizes[] = { 1, 2, 3, 4, 7, 8, 13, 16, 31, 32 };
    for (size_t k = 0; k < sizeof(sizes) / sizeof(sizes[0]); ++k) {
        size_t size = sizes[k];
        CHECK(size <= MSG_MAX);
        struct msgbox* box = NewBox(size, 3);

        for (uint32_t seed = 1; seed <= 3; ++seed) {
            uint8_t m[MSG_MAX];
            FillMsg(m, size, seed);
            CHECK(KePostMessage(box, m, TIMEOUT_INSTANT) == 0);
            CHECK(MsgMatches(m, size, seed));         /* the source was not modified */
        }
        ExpectFull(box, size);
        for (uint32_t seed = 1; seed <= 3; ++seed) {
            ExpectGet(box, size, seed);
        }
        ExpectEmpty(box, size);
        DerefObject(box);
    }
}

static void TestFifoOrder(void) {
    struct msgbox* box = NewBox(SZ, 8);
    for (uint32_t s = 10; s < 18; ++s) {
        ExpectPost(box, SZ, s);
    }
    for (uint32_t s = 10; s < 18; ++s) {
        ExpectGet(box, SZ, s);
    }
    ExpectEmpty(box, SZ);
    DerefObject(box);
}

static void TestPeekDoesNotRemove(void) {
    struct msgbox* box = NewBox(SZ, 4);
    ExpectPost(box, SZ, 1);
    ExpectPost(box, SZ, 2);

    ExpectPeek(box, SZ, 1);
    ExpectPeek(box, SZ, 1);     /* still there */
    ExpectGet(box, SZ, 1);
    ExpectPeek(box, SZ, 2);     /* the head moved on */
    ExpectGet(box, SZ, 2);
    ExpectEmpty(box, SZ);

    /* Peeking must not have used up a slot or a message. */
    for (uint32_t s = 1; s <= 4; ++s) {
        ExpectPost(box, SZ, s);
    }
    ExpectFull(box, SZ);
    ExpectPeek(box, SZ, 1);
    ExpectFull(box, SZ);
    for (uint32_t s = 1; s <= 4; ++s) {
        ExpectGet(box, SZ, s);
    }
    ExpectEmpty(box, SZ);
    DerefObject(box);
}

static void TestCapacityLimit(void) {
    struct msgbox* box = NewBox(SZ, 3);
    ExpectPost(box, SZ, 1);
    ExpectPost(box, SZ, 2);
    ExpectPost(box, SZ, 3);
    ExpectFull(box, SZ);

    ExpectGet(box, SZ, 1);
    ExpectPost(box, SZ, 4);
    ExpectFull(box, SZ);        /* the refused post above was not stored */

    ExpectGet(box, SZ, 2);
    ExpectGet(box, SZ, 3);
    ExpectGet(box, SZ, 4);
    ExpectEmpty(box, SZ);
    DerefObject(box);
}

static void TestCapacityOne(void) {
    struct msgbox* box = NewBox(SZ, 1);
    for (uint32_t round = 1; round <= 6; ++round) {
        ExpectPost(box, SZ, round);
        ExpectFull(box, SZ);
        ExpectPeek(box, SZ, round);
        ExpectGet(box, SZ, round);
        ExpectEmpty(box, SZ);
    }
    DerefObject(box);
}

static void TestInvalidArguments(void) {
    struct msgbox* box = NewBox(SZ, 2);
    uint8_t m[SZ] = { 0 };
    uint8_t out[SZ];
    struct msgbox* boxes[1] = { box };

    CHECK(KePostMessage(NULL, m, TIMEOUT_INSTANT) == EINVAL);
    CHECK(KePostMessage(box, NULL, TIMEOUT_INSTANT) == EINVAL);
    CHECK(KeGetMessage(box, NULL, TIMEOUT_INSTANT) == EINVAL);
    CHECK(KePeekMessage(box, NULL, TIMEOUT_INSTANT) == EINVAL);
    CHECK(KeTryReplaceOrAdd(NULL, m, m, TIMEOUT_INSTANT) == EINVAL);
    CHECK(KeTryReplaceOrAdd(box, NULL, m, TIMEOUT_INSTANT) == EINVAL);
    CHECK(KeTryReplaceOrAdd(box, m, NULL, TIMEOUT_INSTANT) == EINVAL);
    CHECK(KeGetMessageFromMany(NULL, 1, out, TIMEOUT_INSTANT, true, NULL) == EINVAL);
    CHECK(KeGetMessageFromMany(boxes, 1, NULL, TIMEOUT_INSTANT, true, NULL) == EINVAL);

    ExpectEmpty(box, SZ);       /* none of the rejected calls stored anything */
    DerefObject(box);
}

/*
 * Single-threaded, model-based: random posts, gets, peeks and replace-or-adds
 * checked step by step against a plain FIFO array. This is what exercises the
 * ring buffer indices, including wrap-around, for several capacities.
 */
#define MODEL_MAX 16

static uint32_t Rand(uint32_t* s) {
    *s = *s * 1664525u + 1013904223u;
    return *s >> 8;
}

static void ModelTest(size_t cap, uint32_t seed0) {
    struct msgbox* box = NewBox(SZ, cap);
    uint32_t q[MODEL_MAX];
    int head = 0;
    int count = 0;
    uint32_t rng = seed0;
    uint32_t next = 1;
    struct gbuf g;

    for (int step = 0; step < 400; ++step) {
        switch (Rand(&rng) % 7) {
        case 0:
        case 1:
        case 2: {
            int r = PostSeed(box, SZ, next, TIMEOUT_INSTANT);
            if (count < (int) cap) {
                CHECK(r == 0);
                q[(head + count) % MODEL_MAX] = next;
                ++count;
            } else {
                CHECK(r == EAGAIN);
            }
            ++next;
            break;
        }

        case 3:
        case 4: {
            GbufInit(&g);
            int r = KeGetMessage(box, GbufData(&g), TIMEOUT_INSTANT);
            if (count > 0) {
                CHECK(r == 0);
                CHECK(MsgMatches(GbufData(&g), SZ, q[head]));
                GbufCheckGuards(&g, SZ);
                head = (head + 1) % MODEL_MAX;
                --count;
            } else {
                CHECK(r == EAGAIN);
            }
            break;
        }

        case 5: {
            GbufInit(&g);
            int r = KePeekMessage(box, GbufData(&g), TIMEOUT_INSTANT);
            if (count > 0) {
                CHECK(r == 0);
                CHECK(MsgMatches(GbufData(&g), SZ, q[head]));
            } else {
                CHECK(r == EAGAIN);
            }
            break;
        }

        default: {
            /* Replace-or-add, only while there is a free slot (see the note on
             * TestReplaceOnFullBoxTailMatch for why not when full). */
            if (count < (int) cap) {
                bool match = count > 0 && (Rand(&rng) & 1);
                int tail = (head + count - 1) % MODEL_MAX;
                uint32_t cmp = match ? q[tail] : 0xDEAD0000u + (uint32_t) step;
                CHECK(Replace(box, SZ, cmp, next, TIMEOUT_INSTANT) == 0);
                if (match) {
                    q[tail] = next;
                } else {
                    q[(head + count) % MODEL_MAX] = next;
                    ++count;
                }
                ++next;
            }
            break;
        }
        }
    }

    while (count > 0) {
        ExpectGet(box, SZ, q[head]);
        head = (head + 1) % MODEL_MAX;
        --count;
    }
    ExpectEmpty(box, SZ);
    DerefObject(box);
}

static void TestModelRandomised(void) {
    static const size_t caps[] = { 1, 2, 3, 5, 8 };
    for (size_t k = 0; k < sizeof(caps) / sizeof(caps[0]); ++k) {
        ModelTest(caps[k], 0x1234u + (uint32_t) k * 7919u);
    }
}

/* ------------------------------------------------------------------------ */
/* KeTryReplaceOrAdd                                                         */
/* ------------------------------------------------------------------------ */

static void TestReplaceOnEmptyAdds(void) {
    struct msgbox* box = NewBox(SZ, 4);
    CHECK(Replace(box, SZ, 99, 1, TIMEOUT_INSTANT) == 0);
    ExpectGet(box, SZ, 1);
    ExpectEmpty(box, SZ);
    DerefObject(box);
}

static void TestReplaceTail(void) {
    struct msgbox* box = NewBox(SZ, 4);
    ExpectPost(box, SZ, 1);
    CHECK(Replace(box, SZ, 1, 2, TIMEOUT_INSTANT) == 0);   /* replaced in place */
    ExpectGet(box, SZ, 2);
    ExpectEmpty(box, SZ);                                  /* ...not added */
    DerefObject(box);
}

static void TestReplaceMismatchAppends(void) {
    struct msgbox* box = NewBox(SZ, 4);
    ExpectPost(box, SZ, 1);
    CHECK(Replace(box, SZ, 99, 2, TIMEOUT_INSTANT) == 0);
    ExpectGet(box, SZ, 1);
    ExpectGet(box, SZ, 2);
    ExpectEmpty(box, SZ);
    DerefObject(box);
}

/* Only the newest message is a candidate for replacement. */
static void TestReplaceOnlyComparesTail(void) {
    struct msgbox* box = NewBox(SZ, 4);
    ExpectPost(box, SZ, 1);
    ExpectPost(box, SZ, 2);

    CHECK(Replace(box, SZ, 1, 3, TIMEOUT_INSTANT) == 0);   /* 1 is the head, not the tail: appended */
    CHECK(Replace(box, SZ, 3, 4, TIMEOUT_INSTANT) == 0);   /* 3 is now the tail: replaced */

    ExpectGet(box, SZ, 1);
    ExpectGet(box, SZ, 2);
    ExpectGet(box, SZ, 4);
    ExpectEmpty(box, SZ);
    DerefObject(box);
}

/* A replace uses a slot temporarily and must give it back. */
static void TestReplaceKeepsCapacity(void) {
    struct msgbox* box = NewBox(SZ, 2);
    ExpectPost(box, SZ, 1);
    uint32_t cur = 1;
    for (int i = 0; i < 10; ++i) {
        CHECK(Replace(box, SZ, cur, cur + 1, TIMEOUT_INSTANT) == 0);
        ++cur;
    }
    ExpectPost(box, SZ, 99);        /* a leaked slot would make the box look full here */
    ExpectFull(box, SZ);
    ExpectGet(box, SZ, cur);
    ExpectGet(box, SZ, 99);
    ExpectEmpty(box, SZ);
    DerefObject(box);
}

/* The tail is at index capacity-1 while the write index has wrapped to 0. */
static void TestReplaceAfterWrap(void) {
    struct msgbox* box = NewBox(SZ, 3);
    ExpectPost(box, SZ, 1);
    ExpectGet(box, SZ, 1);
    ExpectPost(box, SZ, 2);
    ExpectPost(box, SZ, 3);                                 /* write index wrapped to 0 */
    CHECK(Replace(box, SZ, 3, 4, TIMEOUT_INSTANT) == 0);
    ExpectGet(box, SZ, 2);
    ExpectGet(box, SZ, 4);
    ExpectEmpty(box, SZ);
    DerefObject(box);
}

static void TestReplaceFullBoxMismatch(void) {
    struct msgbox* box = NewBox(SZ, 2);
    ExpectPost(box, SZ, 1);
    ExpectPost(box, SZ, 2);
    CHECK(Replace(box, SZ, 99, 3, TIMEOUT_INSTANT) == EAGAIN);   /* needs a slot and there is none */
    ExpectGet(box, SZ, 1);
    ExpectGet(box, SZ, 2);
    ExpectEmpty(box, SZ);
    DerefObject(box);
}

/* Adding via replace-or-add must wake a reader like a normal post. */
static void TestReplaceWakesReader(void) {
    struct msgbox* box = NewBox(SZ, 2);
    struct tjob* r = JobGet(box);
    Start(r);
    ExpectBlocked(r);
    CHECK(Replace(box, SZ, 99, 5, TIMEOUT_INSTANT) == 0);
    ExpectJobMsg(r, SZ, 5);
    ExpectEmpty(box, SZ);
    DerefObject(box);
}

/* Full box, and the message doesn't match the tail, so the add has to wait for space. */
static void TestReplaceBlocksUntilSpace(void) {
    struct msgbox* box = NewBox(SZ, 2);
    ExpectPost(box, SZ, 1);
    ExpectPost(box, SZ, 2);

    struct tjob* j = JobReplace(box, 99, 3);
    Start(j);
    ExpectBlocked(j);

    ExpectGet(box, SZ, 1);          /* frees a slot */
    WaitDone(j);
    CHECK(j->result == 0);
    ExpectGet(box, SZ, 2);
    ExpectGet(box, SZ, 3);
    ExpectEmpty(box, SZ);
    DerefObject(box);
}

/* ------------------------------------------------------------------------ */
/* Blocking, one box                                                         */
/* ------------------------------------------------------------------------ */

static void TestBlockedReaderWokenByPost(void) {
    struct msgbox* box = NewBox(SZ, 4);
    struct tjob* r = JobGet(box);
    Start(r);
    ExpectBlocked(r);

    ExpectPost(box, SZ, 7);
    ExpectJobMsg(r, SZ, 7);
    ExpectEmpty(box, SZ);
    DerefObject(box);
}

static void TestBlockedWriterWokenByGet(void) {
    struct msgbox* box = NewBox(SZ, 2);
    ExpectPost(box, SZ, 1);
    ExpectPost(box, SZ, 2);

    struct tjob* w = JobPost(box, 3);
    Start(w);
    ExpectBlocked(w);

    ExpectGet(box, SZ, 1);          /* frees a slot, handing it to the writer */
    WaitDone(w);
    CHECK(w->result == 0);

    ExpectGet(box, SZ, 2);
    ExpectGet(box, SZ, 3);
    ExpectEmpty(box, SZ);
    DerefObject(box);
}

static void TestBlockedPeekerWokenByPost(void) {
    struct msgbox* box = NewBox(SZ, 4);
    struct tjob* p = JobPeek(box);
    Start(p);
    ExpectBlocked(p);

    ExpectPost(box, SZ, 5);
    ExpectJobMsg(p, SZ, 5);
    ExpectGet(box, SZ, 5);          /* the peek left it in place */
    ExpectEmpty(box, SZ);
    DerefObject(box);
}

/* One message, two peekers: the permit must be passed on, so both see it. */
static void TestTwoPeekersOnePost(void) {
    struct msgbox* box = NewBox(SZ, 4);
    struct tjob* p1 = JobPeek(box);
    struct tjob* p2 = JobPeek(box);
    Start(p1);
    Start(p2);
    ExpectBlocked(p1);
    ExpectBlocked(p2);

    ExpectPost(box, SZ, 9);
    ExpectJobMsg(p1, SZ, 9);
    ExpectJobMsg(p2, SZ, 9);
    ExpectGet(box, SZ, 9);
    ExpectEmpty(box, SZ);
    DerefObject(box);
}

/* A peeker queued ahead of a getter passes the message on to it. */
static void TestPeekerThenGetter(void) {
    struct msgbox* box = NewBox(SZ, 4);
    struct tjob* p = JobPeek(box);
    struct tjob* g = JobGet(box);
    Start(p);
    Start(g);
    ExpectBlocked(p);
    ExpectBlocked(g);

    ExpectPost(box, SZ, 3);
    ExpectJobMsg(p, SZ, 3);
    ExpectJobMsg(g, SZ, 3);
    ExpectEmpty(box, SZ);
    DerefObject(box);
}

/* A getter queued ahead of a peeker takes the message, so the peeker keeps waiting. */
static void TestGetterThenPeeker(void) {
    struct msgbox* box = NewBox(SZ, 4);
    struct tjob* g = JobGet(box);
    struct tjob* p = JobPeek(box);
    Start(g);
    Start(p);
    ExpectBlocked(g);
    ExpectBlocked(p);

    ExpectPost(box, SZ, 1);
    ExpectJobMsg(g, SZ, 1);
    ExpectBlocked(p);               /* nothing left for the peeker */

    ExpectPost(box, SZ, 2);
    ExpectJobMsg(p, SZ, 2);
    ExpectGet(box, SZ, 2);
    ExpectEmpty(box, SZ);
    DerefObject(box);
}

/* Several readers, several messages: every message is delivered exactly once. */
static void TestManyReadersManyPosts(void) {
    enum { N = 4 };
    struct msgbox* box = NewBox(SZ, N);
    struct tjob* r[N];
    for (int i = 0; i < N; ++i) {
        r[i] = JobGet(box);
        Start(r[i]);
    }
    for (int i = 0; i < N; ++i) {
        ExpectBlocked(r[i]);
    }

    for (uint32_t s = 1; s <= N; ++s) {
        ExpectPost(box, SZ, s);
    }

    int seen[N + 1] = { 0 };
    for (int i = 0; i < N; ++i) {
        WaitDone(r[i]);
        CHECK(r[i]->result == 0);
        int matched = 0;
        for (uint32_t s = 1; s <= N; ++s) {
            if (MsgMatches(r[i]->out, SZ, s)) {
                ++seen[s];
                ++matched;
            }
        }
        CHECK(matched == 1);
    }
    for (int s = 1; s <= N; ++s) {
        CHECK(seen[s] == 1);
    }
    ExpectEmpty(box, SZ);
    DerefObject(box);
}

/* Several writers blocked on a full box: older messages first, nothing lost. */
static void TestManyWritersBlocked(void) {
    enum { W = 3 };
    struct msgbox* box = NewBox(SZ, 2);
    ExpectPost(box, SZ, 1);
    ExpectPost(box, SZ, 2);

    struct tjob* w[W];
    for (int i = 0; i < W; ++i) {
        w[i] = JobPost(box, 11 + (uint32_t) i);
        Start(w[i]);
    }
    for (int i = 0; i < W; ++i) {
        ExpectBlocked(w[i]);
    }

    struct gbuf g;
    GbufInit(&g);
    GetWait(box, GbufData(&g));
    CHECK(MsgMatches(GbufData(&g), SZ, 1));
    GetWait(box, GbufData(&g));
    CHECK(MsgMatches(GbufData(&g), SZ, 2));

    int seen[W] = { 0 };
    for (int i = 0; i < W; ++i) {
        GetWait(box, GbufData(&g));
        int matched = 0;
        for (int k = 0; k < W; ++k) {
            if (MsgMatches(GbufData(&g), SZ, 11 + (uint32_t) k)) {
                ++seen[k];
                ++matched;
            }
        }
        CHECK(matched == 1);
    }
    for (int k = 0; k < W; ++k) {
        CHECK(seen[k] == 1);
        WaitDone(w[k]);
        CHECK(w[k]->result == 0);
    }
    ExpectEmpty(box, SZ);
    DerefObject(box);
}

/* One producer thread, the test thread consuming, box much smaller than the stream. */
static void TestProducerThreadOrder(void) {
    enum { N = 40 };
    struct msgbox* box = NewBox(SZ, 3);
    struct tjob* p = NewJob(JOB_SEQ_PRODUCE);
    p->box = box;
    p->count = N;
    p->base = 100;
    Start(p);

    struct gbuf g;
    for (int i = 0; i < N; ++i) {
        GbufInit(&g);
        GetWait(box, GbufData(&g));
        CHECK(MsgMatches(GbufData(&g), SZ, 100 + (uint32_t) i));
        GbufCheckGuards(&g, SZ);
    }
    WaitDone(p);
    CHECK(p->result == 0);
    CHECK(p->progress == N);
    ExpectEmpty(box, SZ);
    DerefObject(box);
}

/* The other way round: the test thread produces, one consumer thread checks order. */
static void TestConsumerThreadOrder(void) {
    enum { N = 40 };
    struct msgbox* box = NewBox(SZ, 3);
    struct tjob* c = NewJob(JOB_SEQ_CONSUME);
    c->box = box;
    c->count = N;
    c->base = 200;
    Start(c);

    for (int i = 0; i < N; ++i) {
        PostWait(box, SZ, 200 + (uint32_t) i);
    }
    WaitDone(c);
    CHECK(c->result == 0);          /* negative values mean "wrong message at position i" */
    CHECK(c->progress == N);
    ExpectEmpty(box, SZ);
    DerefObject(box);
}

/* ------------------------------------------------------------------------ */
/* Several boxes (KeGetMessageFromMany)                                      */
/* ------------------------------------------------------------------------ */

static int GetMany(struct msgbox** boxes, int n, struct gbuf* g, bool remove, int* bo) {
    GbufInit(g);
    return KeGetMessageFromMany(boxes, n, GbufData(g), TIMEOUT_INSTANT, remove, bo);
}

/* When several boxes have messages, the lowest index wins. */
static void TestGetManyLowestIndexWins(void) {
    struct msgbox* A = NewBox(SZ, 4);
    struct msgbox* B = NewBox(SZ, 4);
    struct msgbox* ab[2] = { A, B };
    struct msgbox* ba[2] = { B, A };
    struct gbuf g;
    int bo;

    ExpectPost(B, SZ, 2);
    bo = -77;
    CHECK(GetMany(ab, 2, &g, true, &bo) == 0);
    CHECK(bo == 1);
    CHECK(MsgMatches(GbufData(&g), SZ, 2));

    bo = -77;
    CHECK(GetMany(ab, 2, &g, true, &bo) == EAGAIN);
    CHECK(bo == -77);               /* untouched on failure */
    GbufCheckUntouched(&g);

    ExpectPost(A, SZ, 1);
    ExpectPost(B, SZ, 3);
    CHECK(GetMany(ab, 2, &g, true, &bo) == 0);
    CHECK(bo == 0);
    CHECK(MsgMatches(GbufData(&g), SZ, 1));
    CHECK(GetMany(ab, 2, &g, true, &bo) == 0);
    CHECK(bo == 1);
    CHECK(MsgMatches(GbufData(&g), SZ, 3));

    /* The index is into the array passed in, not some fixed box order. */
    ExpectPost(A, SZ, 4);
    ExpectPost(B, SZ, 5);
    CHECK(GetMany(ba, 2, &g, true, &bo) == 0);
    CHECK(bo == 0);
    CHECK(MsgMatches(GbufData(&g), SZ, 5));
    CHECK(GetMany(ba, 2, &g, true, &bo) == 0);
    CHECK(bo == 1);
    CHECK(MsgMatches(GbufData(&g), SZ, 4));

    ExpectEmpty(A, SZ);
    ExpectEmpty(B, SZ);
    DerefObject(A);
    DerefObject(B);
}

static void TestGetManyNullBoxOut(void) {
    struct msgbox* A = NewBox(SZ, 4);
    struct msgbox* B = NewBox(SZ, 4);
    struct msgbox* ab[2] = { A, B };
    struct gbuf g;
    ExpectPost(B, SZ, 8);
    CHECK(GetMany(ab, 2, &g, true, NULL) == 0);
    CHECK(MsgMatches(GbufData(&g), SZ, 8));
    DerefObject(A);
    DerefObject(B);
}

static void TestGetManyLeavesOtherBoxes(void) {
    struct msgbox* A = NewBox(SZ, 4);
    struct msgbox* B = NewBox(SZ, 4);
    struct msgbox* ab[2] = { A, B };
    struct gbuf g;
    int bo;

    ExpectPost(A, SZ, 1);
    ExpectPost(B, SZ, 2);
    CHECK(GetMany(ab, 2, &g, true, &bo) == 0);
    CHECK(bo == 0);
    CHECK(MsgMatches(GbufData(&g), SZ, 1));

    ExpectEmpty(A, SZ);
    ExpectGet(B, SZ, 2);            /* B's message was not touched */
    ExpectEmpty(B, SZ);
    DerefObject(A);
    DerefObject(B);
}

static void TestPeekManyKeepsMessage(void) {
    struct msgbox* A = NewBox(SZ, 4);
    struct msgbox* B = NewBox(SZ, 4);
    struct msgbox* ab[2] = { A, B };
    struct gbuf g;
    int bo;

    ExpectPost(B, SZ, 6);
    for (int i = 0; i < 3; ++i) {
        bo = -77;
        CHECK(GetMany(ab, 2, &g, false, &bo) == 0);
        CHECK(bo == 1);
        CHECK(MsgMatches(GbufData(&g), SZ, 6));
    }
    ExpectGet(B, SZ, 6);            /* still there after the peeks */
    ExpectEmpty(B, SZ);
    ExpectEmpty(A, SZ);
    DerefObject(A);
    DerefObject(B);
}

/* The waiter must be fully removed from the other boxes' queues once it wakes. */
static void TestGetManyBlockedThenWoken(void) {
    struct msgbox* A = NewBox(SZ, 4);
    struct msgbox* B = NewBox(SZ, 4);
    struct msgbox* C = NewBox(SZ, 4);
    struct msgbox* abc[3] = { A, B, C };

    struct tjob* j = JobGetMany(abc, 3, true);
    Start(j);
    ExpectBlocked(j);

    ExpectPost(B, SZ, 2);
    WaitDone(j);
    CHECK(j->result == 0);
    CHECK(j->box_out == 1);
    CHECK(MsgMatches(j->out, SZ, 2));

    /* A stale queue entry would swallow these messages by waking a finished thread. */
    ExpectPost(A, SZ, 1);
    ExpectGet(A, SZ, 1);
    ExpectPost(C, SZ, 3);
    ExpectGet(C, SZ, 3);
    ExpectPost(B, SZ, 4);
    ExpectGet(B, SZ, 4);

    ExpectEmpty(A, SZ);
    ExpectEmpty(B, SZ);
    ExpectEmpty(C, SZ);
    DerefObject(A);
    DerefObject(B);
    DerefObject(C);
}

static void TestTwoMultiGetters(void) {
    struct msgbox* A = NewBox(SZ, 4);
    struct msgbox* B = NewBox(SZ, 4);
    struct msgbox* ab[2] = { A, B };

    struct tjob* g1 = JobGetMany(ab, 2, true);
    struct tjob* g2 = JobGetMany(ab, 2, true);
    Start(g1);
    Start(g2);
    ExpectBlocked(g1);
    ExpectBlocked(g2);

    ExpectPost(A, SZ, 1);
    ExpectPost(B, SZ, 2);
    WaitDone(g1);
    WaitDone(g2);

    CHECK(g1->result == 0);
    CHECK(g2->result == 0);
    CHECK(g1->box_out != g2->box_out);                 /* each took a different message */
    CHECK(g1->box_out == 0 || g1->box_out == 1);
    CHECK(g2->box_out == 0 || g2->box_out == 1);
    CHECK(MsgMatches(g1->out, SZ, (uint32_t) g1->box_out + 1));   /* and the box it names is the box it read */
    CHECK(MsgMatches(g2->out, SZ, (uint32_t) g2->box_out + 1));

    ExpectEmpty(A, SZ);
    ExpectEmpty(B, SZ);
    DerefObject(A);
    DerefObject(B);
}

/* A blocked multi-peek wakes, reports the box, and leaves the message in it. */
static void TestPeekManyBlockedThenWoken(void) {
    struct msgbox* A = NewBox(SZ, 4);
    struct msgbox* B = NewBox(SZ, 4);
    struct msgbox* ab[2] = { A, B };

    struct tjob* j = JobGetMany(ab, 2, false);
    Start(j);
    ExpectBlocked(j);

    ExpectPost(A, SZ, 5);
    WaitDone(j);
    CHECK(j->result == 0);
    CHECK(j->box_out == 0);
    CHECK(MsgMatches(j->out, SZ, 5));

    ExpectGet(A, SZ, 5);
    ExpectEmpty(A, SZ);
    ExpectEmpty(B, SZ);
    DerefObject(A);
    DerefObject(B);
}

/* ------------------------------------------------------------------------ */
/* Stress                                                                    */
/* ------------------------------------------------------------------------ */

#define PC_MAX_P   4
#define PC_MAX_K   32
#define PC_MAX_BOX 4
#define PC_MAX_J   8

/*
 * `producers` threads each post `per_producer` tagged messages spread over
 * `nboxes` boxes; `consumers` threads take them with KeGetMessageFromMany.
 * Afterwards: every message arrived exactly once, came from the box it says it
 * did, and each producer's messages to each box arrived in the order sent.
 * (The log order is the dequeue order, which holds because scheduling is
 * cooperative.)
 */
static void RunStress(int producers, int consumers, int nboxes, size_t cap, int per_producer) {
    CHECK(producers <= PC_MAX_P && per_producer <= PC_MAX_K && nboxes <= PC_MAX_BOX);
    CHECK(producers + consumers <= PC_MAX_J);
    struct msgbox* boxes[PC_MAX_BOX];
    for (int b = 0; b < nboxes; ++b) {
        boxes[b] = NewBox(sizeof(struct pc_msg), cap);
    }

    int total = producers * per_producer;
    g_log_n = 0;
    g_tickets = total;

    struct tjob* jobs[PC_MAX_J];
    int nj = 0;
    for (int c = 0; c < consumers; ++c) {
        struct tjob* j = NewJob(JOB_PC_CONSUME);
        j->boxes = boxes;
        j->nboxes = nboxes;
        jobs[nj++] = j;
        Start(j);
    }
    for (int p = 0; p < producers; ++p) {
        struct tjob* j = NewJob(JOB_PC_PRODUCE);
        j->boxes = boxes;
        j->nboxes = nboxes;
        j->producer_id = p;
        j->count = per_producer;
        jobs[nj++] = j;
        Start(j);
    }

    for (int i = 0; i < nj; ++i) {
        WAIT_FOR_N(jobs[i]->done, SPIN_LIMIT_LONG);
        CHECK(jobs[i]->result == 0);
    }

    CHECK(g_log_n == total);
    CHECK(g_tickets == 0);

    static uint8_t seen[PC_MAX_P][PC_MAX_K];
    static int last[PC_MAX_P][PC_MAX_BOX];
    memset(seen, 0, sizeof(seen));
    for (int p = 0; p < PC_MAX_P; ++p) {
        for (int b = 0; b < PC_MAX_BOX; ++b) {
            last[p][b] = -1;
        }
    }
    for (int i = 0; i < g_log_n; ++i) {
        struct pc_log* e = &g_log[i];
        CHECK(e->producer < (uint32_t) producers);
        CHECK(e->seq < (uint32_t) per_producer);
        CHECK(e->box < (uint32_t) nboxes);
        CHECK(!seen[e->producer][e->seq]);                       /* no duplicates */
        seen[e->producer][e->seq] = 1;
        CHECK((int) e->box == e->box_out);                       /* from the box it was reported from */
        CHECK((int) e->box == (int) ((e->seq * 7 + e->producer) % (uint32_t) nboxes));
        CHECK((int) e->seq > last[e->producer][e->box]);         /* per producer, per box, in order */
        last[e->producer][e->box] = (int) e->seq;
    }

    for (int b = 0; b < nboxes; ++b) {
        ExpectEmpty(boxes[b], sizeof(struct pc_msg));
        DerefObject(boxes[b]);
    }
}

static void TestStressOneBox(void)        { RunStress(3, 3, 1, 4, 25); }
static void TestStressCapacityOne(void)   { RunStress(2, 2, 1, 1, 15); }
static void TestStressThreeBoxes(void)    { RunStress(3, 2, 3, 2, 20); }
static void TestStressManyConsumers(void) { RunStress(1, 4, 2, 3, 30); }

/* ------------------------------------------------------------------------ */
/* Destroying a box                                                          */
/* ------------------------------------------------------------------------ */

static void TestDestroyWithMessages(void) {
    struct msgbox* box = NewBox(SZ, 4);
    ExpectPost(box, SZ, 1);
    ExpectPost(box, SZ, 2);
    ExpectPost(box, SZ, 3);
    DerefObject(box);                /* must not crash */
}

static void TestDestroyFullBox(void) {
    struct msgbox* box = NewBox(SZ, 2);
    ExpectPost(box, SZ, 1);
    ExpectPost(box, SZ, 2);
    DerefObject(box);
}

/* ------------------------------------------------------------------------ */
/* Suspected bugs (soft checks: these log and carry on)                      */
/* ------------------------------------------------------------------------ */

/*
 * CleanupMsgbox frees the data and the box but never drops its three objects
 * (the mutex and the two semaphores). Destroying a box therefore (a) leaks them
 * and (b) never wakes threads blocked on it, which is exactly what AbandonSem is
 * for. A reader or writer that is waiting when the last reference goes would
 * wait forever. The fix is to DerefObject(lock), DerefObject(empty_sem) and
 * DerefObject(filled_sem) in CleanupMsgbox.
 */
static void TestDestroyWakesBlockedReader(void) {
    struct msgbox* box = NewBox(SZ, 4);
    struct tjob* r = JobGet(box);
    Start(r);
    ExpectBlocked(r);

    DerefObject(box);

    int ok;
    WAIT_RESULT_N(r->done, ok, SPIN_LIMIT);
    SUSPECT(ok, "destroying a message box never wakes a reader blocked on it");
    if (ok) {
        SUSPECT(r->result == EABANDONED, "a reader woken by box destruction should get EABANDONED");
    }
}

static void TestDestroyWakesBlockedWriter(void) {
    struct msgbox* box = NewBox(SZ, 2);
    ExpectPost(box, SZ, 1);
    ExpectPost(box, SZ, 2);
    struct tjob* w = JobPost(box, 3);
    Start(w);
    ExpectBlocked(w);

    DerefObject(box);

    int ok;
    WAIT_RESULT_N(w->done, ok, SPIN_LIMIT);
    SUSPECT(ok, "destroying a message box never wakes a writer blocked on it");
    if (ok) {
        SUSPECT(w->result == EABANDONED, "a writer woken by box destruction should get EABANDONED");
    }
}

/* Destroying one box of a multi-box wait must wake the waiter and leave the other box clean. */
static void TestDestroyOneBoxOfMany(void) {
    struct msgbox* A = NewBox(SZ, 4);
    struct msgbox* B = NewBox(SZ, 4);
    struct msgbox* ab[2] = { A, B };
    struct tjob* j = JobGetMany(ab, 2, true);
    Start(j);
    ExpectBlocked(j);

    DerefObject(A);

    int ok;
    WAIT_RESULT_N(j->done, ok, SPIN_LIMIT);
    SUSPECT(ok, "destroying one box of a multi-box wait never wakes the waiter");
    if (ok) {
        SUSPECT(j->result == EABANDONED, "a multi-box waiter woken by destruction should get EABANDONED");
        ExpectEmpty(B, SZ);
        ExpectPost(B, SZ, 1);        /* a stale queue entry on B would swallow this */
        ExpectGet(B, SZ, 1);
    }
    DerefObject(B);
}

/* count == 0 should be rejected; today it falls through into the semaphore code. */
static void TestGetManyCountZero(void) {
    struct msgbox* box = NewBox(SZ, 2);
    struct msgbox* boxes[1] = { box };
    uint8_t out[SZ];
    int r = KeGetMessageFromMany(boxes, 0, out, TIMEOUT_INSTANT, true, NULL);
    TRACE("  count == 0 returned %d\n", r);
    SUSPECT(r == EINVAL, "KeGetMessageFromMany with count == 0 is not rejected with EINVAL");
    DerefObject(box);
}

/*
 * Design question as much as a bug: KeTryReplaceOrAdd takes a free slot before
 * it looks at the tail, so on a full box it fails (EAGAIN) even when the tail
 * matches and a replace would not need any space.
 */
static void TestReplaceOnFullBoxTailMatch(void) {
    struct msgbox* box = NewBox(SZ, 2);
    ExpectPost(box, SZ, 1);
    ExpectPost(box, SZ, 2);

    int r = Replace(box, SZ, 2, 3, TIMEOUT_INSTANT);
    TRACE("  replace on full box returned %d\n", r);
    SUSPECT(r == 0, "KeTryReplaceOrAdd on a full box fails even though the tail matches");

    struct gbuf g;
    GbufInit(&g);
    CHECK(KeGetMessage(box, GbufData(&g), TIMEOUT_INSTANT) == 0);
    CHECK(MsgMatches(GbufData(&g), SZ, 1));
    CHECK(KeGetMessage(box, GbufData(&g), TIMEOUT_INSTANT) == 0);
    CHECK(MsgMatches(GbufData(&g), SZ, r == 0 ? 3 : 2));
    DerefObject(box);
}

/* These dereference NULL today, so they are opt-in. */
static void TestNullBoxInGetAndPeek(void) {
    uint8_t out[SZ];
    SUSPECT(KeGetMessage(NULL, out, TIMEOUT_INSTANT) == EINVAL, "KeGetMessage(NULL box) is not rejected");
    SUSPECT(KePeekMessage(NULL, out, TIMEOUT_INSTANT) == EINVAL, "KePeekMessage(NULL box) is not rejected");
}

/*
 * Two leaks are suspected: CleanupMsgbox doesn't drop the mutex and semaphores,
 * and KeGetMessageFromMany allocates a semaphore array it never frees.
 */
static void TestNoLeakOnDestroy(void) {
    struct msgbox* warm = NewBox(SZ, 4);
    DerefObject(warm);

    size_t before = GetHeapAllocatedAmount();
    for (int i = 0; i < 20; ++i) {
        struct msgbox* box = NewBox(SZ, 4);
        DerefObject(box);
    }
    size_t after = GetHeapAllocatedAmount();
    TRACE("  heap before %d, after %d\n", (int) before, (int) after);
    SUSPECT(after == before, "creating and destroying message boxes leaks heap");
}

static void TestNoLeakOnGet(void) {
    struct msgbox* box = NewBox(SZ, 4);
    ExpectPost(box, SZ, 1);
    ExpectPeek(box, SZ, 1);      /* warm up */

    size_t before = GetHeapAllocatedAmount();
    for (int i = 0; i < 50; ++i) {
        ExpectPeek(box, SZ, 1);
    }
    size_t after = GetHeapAllocatedAmount();
    TRACE("  heap before %d, after %d\n", (int) before, (int) after);
    SUSPECT(after == before, "KeGetMessageFromMany leaks the semaphore array it allocates");
    DerefObject(box);
}

/* ------------------------------------------------------------------------ */
/* Runner                                                                    */
/* ------------------------------------------------------------------------ */

#define RUN(fn) do { g_njobs = 0; RUN_TEST(fn); } while (0)

void RunMsgboxTests(void) {
    TRACE("msgbox tests: starting\n");
    g_suspected = 0;

    /* Basics */
    RUN(TestCreateDestroy);
    RUN(TestPostGetSingle);
    RUN(TestMessageSizes);
    RUN(TestFifoOrder);
    RUN(TestPeekDoesNotRemove);
    RUN(TestCapacityLimit);
    RUN(TestCapacityOne);
    RUN(TestInvalidArguments);
    RUN(TestModelRandomised);

    /* KeTryReplaceOrAdd */
    RUN(TestReplaceOnEmptyAdds);
    RUN(TestReplaceTail);
    RUN(TestReplaceMismatchAppends);
    RUN(TestReplaceOnlyComparesTail);
    RUN(TestReplaceKeepsCapacity);
    RUN(TestReplaceAfterWrap);
    RUN(TestReplaceFullBoxMismatch);
    RUN(TestReplaceWakesReader);
    RUN(TestReplaceBlocksUntilSpace);

    /* Blocking, one box */
    RUN(TestBlockedReaderWokenByPost);
    RUN(TestBlockedWriterWokenByGet);
    RUN(TestBlockedPeekerWokenByPost);
    RUN(TestTwoPeekersOnePost);
    RUN(TestPeekerThenGetter);
    RUN(TestGetterThenPeeker);
    RUN(TestManyReadersManyPosts);
    RUN(TestManyWritersBlocked);
    RUN(TestProducerThreadOrder);
    RUN(TestConsumerThreadOrder);

    /* Several boxes */
    RUN(TestGetManyLowestIndexWins);
    RUN(TestGetManyNullBoxOut);
    RUN(TestGetManyLeavesOtherBoxes);
    RUN(TestPeekManyKeepsMessage);
    RUN(TestGetManyBlockedThenWoken);
    RUN(TestTwoMultiGetters);
    RUN(TestPeekManyBlockedThenWoken);

    /* Stress */
    RUN(TestStressOneBox);
    RUN(TestStressCapacityOne);
    RUN(TestStressThreeBoxes);
    RUN(TestStressManyConsumers);

    /* Destruction that must at least not crash */
    RUN(TestDestroyWithMessages);
    RUN(TestDestroyFullBox);

    /* Suspected bugs: soft checks, reported at the end */
    RUN(TestDestroyWakesBlockedReader);
    RUN(TestDestroyWakesBlockedWriter);
    RUN(TestDestroyOneBoxOfMany);
    RUN(TestGetManyCountZero);
    RUN(TestReplaceOnFullBoxTailMatch);
    RUN(TestNullBoxInGetAndPeek);
    RUN(TestNoLeakOnDestroy);
    RUN(TestNoLeakOnGet);

    if (g_suspected == 0) {
        TRACE("msgbox tests: ALL PASSED\n");
    } else {
        TRACE("msgbox tests: all checks passed, but %d SUSPECTED BUG(S) were reported above\n", g_suspected);
    }
}