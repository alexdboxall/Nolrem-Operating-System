#include <common.h>
#include <machine/x86.h>
#include <arch.h>
#include <log.h>

/*
 * The performance counter is the CPU's cycle counter (TSC) when there is one,
 * and the PIT when there isn't (i486). Either way the CPU speed has to be
 * worked out against the PIT, as it's the only thing in a PC that tells you the
 * time without already having to know how fast the CPU is.
 */
static bool use_tsc = false;
static uint64_t counter_hz = X86_PIT_FREQUENCY;
static uint32_t cpu_khz = 0;

/* 20ms is plenty: one PIT clock is 0.84us, so the PIT is good to 0.004%. */
#define TSC_MEASURE_CLOCKS      (X86_PIT_FREQUENCY / 50)

/* The speed estimate on an i486 doubles the loop until it takes at least this. */
#define LOOP_MEASURE_CLOCKS     (X86_PIT_FREQUENCY / 100)       /* 10ms */
#define LOOP_MAX_TRIPS          (1u << 28)

/*
 * Anything outside this range means the TSC isn't counting the way we think it
 * is (emulators that don't bother, mostly), so we don't believe it.
 */
#define TSC_MIN_HZ              ((uint64_t) 1000000)            /* 1MHz */
#define TSC_MAX_HZ              ((uint64_t) 100000000000)       /* 100GHz */

/*
 * Counts the TSC against the PIT, with interrupts off so that nothing gets in
 * the way and the PIT counter is only read at the times that we want it to be.
 * Returns 0 if the answer doesn't look right.
 */
static uint64_t x86MeasureTscHz(void) {
    size_t flags = ArchLockToCpu();

    uint64_t pit_start = x86GetPitCounter();
    uint64_t tsc_start = x86ReadTsc();
    uint64_t pit_end;
    do {
        pit_end = x86GetPitCounter();
    } while (pit_end - pit_start < TSC_MEASURE_CLOCKS);
    uint64_t tsc_end = x86ReadTsc();

    ArchUnlockFromCpu(flags);

    uint64_t hz = (tsc_end - tsc_start) * X86_PIT_FREQUENCY / (pit_end - pit_start);
    if (hz < TSC_MIN_HZ || hz > TSC_MAX_HZ) {
        return 0;
    }
    return hz;
}

/*
 * The 486 has nothing to count clocks with, so this goes the other way round:
 * run a loop that takes a known number of clocks, and see how long the PIT says
 * it took. How many clocks a loop takes depends on the make (some of the
 * non-Intel 486 class chips do it quicker, and would read as too fast here) and
 * on a bit of luck with the cache, so it is a rough guide, so it is given to
 * the nearest megahertz.
 */
static uint32_t x86EstimateCpuKhz(void) {
    size_t flags = ArchLockToCpu();

    uint32_t trips = 1u << 10;
    uint64_t elapsed;
    for (;;) {
        uint64_t start = x86GetPitCounter();
        x86DelayLoop(trips);
        elapsed = x86GetPitCounter() - start;

        /* Each run is at most twice the length of the one before, which was
         * under the target, so none can come near a full period of the PIT
         * (which is when it'd lose count). */
        if (elapsed >= LOOP_MEASURE_CLOCKS || trips >= LOOP_MAX_TRIPS) {
            break;
        }
        trips *= 2;
    }

    ArchUnlockFromCpu(flags);

    if (elapsed == 0) {
        return 0;
    }
    uint64_t clocks = (uint64_t) trips * X86_DELAY_LOOP_CLOCKS_PER_TRIP;
    uint64_t khz = clocks * X86_PIT_FREQUENCY / (elapsed * 1000);
    return (uint32_t) (((khz + 500) / 1000) * 1000);
}

void x86InitPerformanceCounter(void) {
    const char* how;

    uint64_t tsc_hz = x86HasTsc() ? x86MeasureTscHz() : 0;
    if (tsc_hz != 0) {
        use_tsc = true;
        counter_hz = tsc_hz;
        cpu_khz = (uint32_t) ((tsc_hz + 500) / 1000);
        how = "measured with the cycle counter";
    } else {
        use_tsc = false;
        counter_hz = X86_PIT_FREQUENCY;
        cpu_khz = x86EstimateCpuKhz();
        how = "estimated";
    }

    KeLogPrintf("CPU: family %d, %d kHz (%s)\n", x86GetCpuFamily(), (int) cpu_khz, how);
}

export uint64_t ArchGetPerformanceCounter(void) {
    if (use_tsc) {
        return x86ReadTsc();
    }
    return x86GetPitCounter();
}

export uint64_t ArchGetPerformanceCounterFrequency(void) {
    return counter_hz;
}

export uint32_t ArchGetCpuSpeed(void) {
    return cpu_khz;
}
