#include <stddef.h>
#include <cpu.h>
#include <heap.h>
#include <machine/x86.h>

size_t ArchLockToCpu(void) {
    size_t flags;
    asm volatile ("pushf\n\tcli\n\tpop %0" : "=r"(flags) : : "memory");
    return flags;
}

void ArchUnlockFromCpu(size_t rv) {
    asm ("push %0\n\tpopf" : : "rm"(rv) : "memory","cc");
}

int ArchGetCpuNum(void) {
    size_t val;
    __asm__ __volatile__("mov %%dr3, %0" : "=r" (val) :: );
    return (int) val;
}

void ArchInitPlatformSpecificData(struct cpu_data* cpu) {
    cpu->platform_specific = AllocPermanentHeap(sizeof(platform_cpu_data_t));
    /* The TSS, GDT and IDT are allocated by x86InitTss/Gdt/Idt, which run on
       the CPU that owns them. */
    cpu->platform_specific->tss = NULL;
    cpu->platform_specific->gdt = NULL;
    cpu->platform_specific->idt = NULL;
    cpu->irq_disable_counter = 0;
    cpu->saved_interrupt_enabled = true;
}