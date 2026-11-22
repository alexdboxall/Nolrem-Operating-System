#include <common.h>
#include <machine/x86.h>
#include <log.h>
#include <interrupt.h>
#include <timer.h>
#include <spinlock.h>
#include <stdatomic.h>

#define PIT_CHANNEL0        0x40
#define PIT_COMMAND         0x43

/* Channel 0, lobyte/hibyte access, mode 2 (rate generator), binary. */
#define PIT_CMD_CH0_RATE    0x34
/* Channel 0, latch the current count so that it can be read in two halves. */
#define PIT_CMD_CH0_LATCH   0x00

#define PIT_IRQ             0

/*
 * How close to the end of a period the count can be for us to believe that the
 * wrap was after we read it, not before. See x86GetPitCounter().
 */
#define PIT_RACE_CLOCKS     64

static uint64_t pit_nanos = 0;
static bool pit_inhibit = false;

/*
 * The counter. The PIT counts down from `pit_divisor` to 1 and starts again; a
 * full period is `pit_divisor` clocks. This is what is turned into a count that
 * keeps going up:
 *
 *      clocks = (periods that have ended) * pit_divisor
 *             + (pit_divisor - count)
 *
 * The periods that have ended are the interrupts that we have handled, plus the
 * one that is waiting if interrupts are off.
 */
static struct spinlock pit_lock;
static uint32_t pit_divisor = 0;
static bool pit_ready = false;
static _Atomic uint32_t pit_irq_count = 0;
static uint64_t pit_last_clocks = 0;            /* protected by pit_lock */

void ArchDebugInhibitTimerAdvance(bool inhibit) {
#ifdef NDEBUG
	(void) inhibit;
#else
	pit_inhibit = inhibit;
#endif
}

static void x86HandlePit(struct x86_regs*) {
	/* First thing, so that anything this ends up calling that reads the
	 * counter sees this period as having ended. */
	atomic_fetch_add_explicit(&pit_irq_count, 1, memory_order_relaxed);

	if (!pit_inhibit) {
		KeAdvanceTimer(pit_nanos);
	}
}

static uint32_t x86ReadPitCount(void) {
	outb(PIT_COMMAND, PIT_CMD_CH0_LATCH);
	uint32_t count = inb(PIT_CHANNEL0);
	count |= (uint32_t) inb(PIT_CHANNEL0) << 8;
	return count;
}

void x86InitPit(int hertz) { 
	uint32_t divisor = X86_PIT_FREQUENCY / hertz;
	outb(PIT_COMMAND, PIT_CMD_CH0_RATE);
	outb(PIT_CHANNEL0, divisor & 0xFF);
	outb(PIT_CHANNEL0, divisor >> 8);
    pit_nanos = (1000000 / hertz) * 1000;

	/*
	 * The new divisor is only loaded on the next clock, and until then the
	 * count is whatever the BIOS left behind (and might be more than ours). It
	 * is under a microsecond, so there is no point in sleeping for it.
	 */
	pit_divisor = divisor;
	for (int tries = 0; tries < 1000; ++tries) {
		uint32_t count = x86ReadPitCount();
		if (count >= 1 && count <= divisor) {
			break;
		}
	}
	atomic_store_explicit(&pit_irq_count, 0, memory_order_relaxed);
	pit_last_clocks = 0;
	pit_ready = true;

    KeRegisterInterruptHandler(PIT_IRQ, x86HandlePit);
}

/*
 * Reading the counter is a race against the timer wrapping round: if it wraps
 * after the count is read but before the interrupt counter is, then the two
 * disagree about which period we are in, and the time jumps by a whole period.
 * (Roughly 40ms, and it is what happens when interrupts are off, which they
 * are in here, so the interrupt can't be handled while we're in the middle.)
 *
 * So after reading the count, we ask the PIC whether the interrupt is waiting.
 * If it is, the period has ended and the interrupt handler hasn't run, so add
 * one. The only thing that can go wrong is the wrap landing between the count
 * being read and the PIC being asked (a few microseconds). That is when the
 * count is read as nearly at the end of its period, so such a count is taken
 * to mean the wrap hasn't happened yet.
 *
 * The result is made to never go backwards, as a last defence against the one
 * case this can't catch: the interrupt being handled by another CPU at the
 * very same time as we read. That can leave the count a period short until the
 * next call, and not go backwards.
 */
uint64_t x86GetPitCounter(void) {
	if (!pit_ready) {
		return 0;
	}

	KeAcquireSpinlock(&pit_lock);

	uint32_t count = x86ReadPitCount();
	bool wrap_pending = x86IsIrqPending(PIT_IRQ);
	uint32_t periods = atomic_load_explicit(&pit_irq_count, memory_order_relaxed);

	if (count < 1) count = 1;
	if (count > pit_divisor) count = pit_divisor;

	if (wrap_pending && count > PIT_RACE_CLOCKS) {
		++periods;
	}

	uint64_t clocks = (uint64_t) periods * pit_divisor + (pit_divisor - count);
	if (clocks < pit_last_clocks) {
		clocks = pit_last_clocks;
	}
	pit_last_clocks = clocks;

	KeReleaseSpinlock(&pit_lock);
	return clocks;
}
