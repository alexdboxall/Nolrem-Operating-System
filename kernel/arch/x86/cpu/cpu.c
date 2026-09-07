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
    cpu->platform_specific = AllocHeap(sizeof(platform_cpu_data_t));
    cpu->platform_specific->tss = AllocHeap(sizeof(struct tss));
}