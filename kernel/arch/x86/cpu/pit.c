#include <common.h>
#include <machine/x86.h>
#include <log.h>
#include <interrupt.h>
#include <timer.h>

static uint64_t pit_nanos = 0;

static void HandlePit(struct x86_regs*) {
    AdvanceTimer(pit_nanos);
}

void x86InitPit(int hertz) { 
	int divisor = 1193180 / hertz;
	outb(0x43, 0x36);
	outb(0x40, divisor & 0xFF);
	outb(0x40, divisor >> 8);
    pit_nanos = (1000000 / hertz) * 1000;
    RegisterInterruptHandler(0, HandlePit);
}
