#include <common.h>
#include <machine/x86.h>

#define X86_EFLAGS_ID       0x00200000      /* can be changed iff CPUID exists */
#define X86_CPUID_TSC       (1u << 4)       /* CPUID.1:EDX */

/*
 * CPUID has a different register to save with position independent code (EBX),
 * so it goes through this rather than being asked for directly.
 */
static void x86Cpuid(uint32_t leaf, uint32_t* a, uint32_t* b, uint32_t* c, uint32_t* d) {
    uint32_t ra, rb, rc, rd;
    __asm__ volatile (
        "xchgl %%ebx, %1\n\t"
        "cpuid\n\t"
        "xchgl %%ebx, %1"
        : "=a"(ra), "=&r"(rb), "=c"(rc), "=d"(rd)
        : "0"(leaf), "2"(0)
        : "memory");
    *a = ra; *b = rb; *c = rc; *d = rd;
}

/*
 * The CPUID instruction came partway through the 486's life, so older ones
 * don't have it. The way to tell is the ID flag in EFLAGS: it can be changed on
 * CPUs that have CPUID, and not on those that don't.
 */
bool x86HasCpuid(void) {
    uint32_t before, after;
    __asm__ volatile (
        "pushfl\n\t"                    /* to put back at the end */
        "pushfl\n\t"
        "popl %0\n\t"
        "movl %0, %1\n\t"
        "xorl %2, %1\n\t"
        "pushl %1\n\t"
        "popfl\n\t"
        "pushfl\n\t"
        "popl %1\n\t"
        "popfl"
        : "=&r"(before), "=&r"(after)
        : "i"(X86_EFLAGS_ID)
        : "cc", "memory");
    return ((before ^ after) & X86_EFLAGS_ID) != 0;
}

bool x86HasTsc(void) {
    if (!x86HasCpuid()) {
        return false;
    }
    uint32_t max_leaf, b, c, d;
    x86Cpuid(0, &max_leaf, &b, &c, &d);
    if (max_leaf < 1) {
        return false;
    }
    uint32_t a;
    x86Cpuid(1, &a, &b, &c, &d);
    return (d & X86_CPUID_TSC) != 0;
}

/*
 * Without CPUID it is taken to be a 486. It can't be a 386, as the entry code
 * stops those, and every Pentium has CPUID.
 */
int x86GetCpuFamily(void) {
    if (!x86HasCpuid()) {
        return 4;
    }
    uint32_t max_leaf, b, c, d;
    x86Cpuid(0, &max_leaf, &b, &c, &d);
    if (max_leaf < 1) {
        return 4;
    }
    uint32_t a;
    x86Cpuid(1, &a, &b, &c, &d);
    int family = (a >> 8) & 0xF;
    if (family == 0xF) {
        family += (a >> 20) & 0xFF;
    }
    return family;
}
