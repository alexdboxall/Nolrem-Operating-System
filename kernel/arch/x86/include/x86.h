#pragma once

#include <stddef.h>
#include <stdint.h>

/*
 * x86-internal declarations. Functions called x86...() belong to the x86
 * architecture only, and must not be called from outside arch/x86.
 *
 * The generic architecture interface that the rest of the kernel uses (all of
 * the Arch...() functions) is declared in <arch.h>.
 */

struct tss;

void x86InitGdt(void);
void x86InitIdt(void);
void x86InitPic(void);
void x86InitPit(int hertz);

/* The 8254 PIT's input clock, in Hz (14.31818MHz / 12). */
#define X86_PIT_FREQUENCY 1193182

/*
 * Number of PIT clocks since x86InitPit(), counting up, or 0 if it has not been
 * called yet. The PIT is the only thing every PC has, so this is the reference
 * that the CPU speed gets measured against.
 *
 * It copes with interrupts being off for up to one period of the PIT interrupt
 * (40ms), but not longer. Never goes backwards.
 */
uint64_t x86GetPitCounter(void);
void x86InitTss(void);
uint16_t x86AddTssToGdt(struct tss* tss);
void FloppyInit(void);

void x86InitCmos(void);
uint8_t x86ReadCmos(uint8_t reg);
void x86WriteCmos(uint8_t reg, uint8_t data);

void Ps2Init(void);

void x86SendPicEoi(int num);
bool x86IsIrqPending(int irq);

/*
 * CPU identification (cpu/cpuid.c).
 *
 * We decide what to use from what the CPU can do, not from what it is called:
 * there are 486s that have CPUID, Cyrix chips with a cycle counter but CPUID
 * turned off, and so on. x86GetCpuFamily() is for display.
 */
bool x86HasCpuid(void);
bool x86HasTsc(void);
int x86GetCpuFamily(void);      /* 4 = 486, 5 = Pentium, 6 = Pentium Pro and later... */

/* Works out the speed of the CPU and which counter ArchGetPerformanceCounter() uses. */
void x86InitPerformanceCounter(void);

struct Elf32_Ehdr;
bool x86IsElfValid(struct Elf32_Ehdr* header);

#define PIC_IRQ_BASE 32

struct x86_regs
{
	/*
	* The registers that are pushed to us in x86/asm/interrupt.s
	* 
	* SS is the first thing pushed, and thus the last to be popped
	* GS is the last thing pushed, and thus the first to be popped
	*/
	size_t gs, fs, es, ds;
	size_t edi, esi, ebp, esp, ebx, edx, ecx, eax;
	size_t int_no, err_code;
	size_t eip, cs, eflags, useresp, ss;
};

static inline void outb(uint16_t port, uint8_t val) {
    __asm__ volatile ("outb %b0, %w1" : : "a"(val), "Nd"(port) : "memory");
}

static inline uint8_t inb(uint16_t port) {
    uint8_t ret;
    __asm__ volatile ("inb %w1, %b0" : "=a"(ret) : "Nd"(port) : "memory");
    return ret;
}

static inline void x86IoDelay(void) {
	__asm__ volatile ("outb %al, $0x80");
}

/* Pentium and later (x86HasTsc()). Not serialised, so it can be reordered a
 * little with respect to nearby instructions. */
static inline uint64_t x86ReadTsc(void) {
    uint64_t value;
    __asm__ volatile ("rdtsc" : "=A"(value));
    return value;
}

/*
 * Burns time: runs `trips` times round a two instruction loop. On a 486 that is
 * 4 clocks a trip (dec = 1, taken jnz = 3), and the alignment keeps it from
 * straddling a prefetch line, which would cost extra. `trips` must not be 0.
 */
#define X86_DELAY_LOOP_CLOCKS_PER_TRIP 4
static inline void x86DelayLoop(uint32_t trips) {
    __asm__ volatile (
        ".p2align 4\n"
        "1:\n\t"
        "decl %0\n\t"
        "jnz 1b"
        : "+r"(trips) : : "cc");
}