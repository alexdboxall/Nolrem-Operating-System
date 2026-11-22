#include <stdint.h>
#include <string.h>
#include <log.h>
#include <common.h>
#include <cpu.h>
#include <arch.h>

struct vas;
struct thread;

static struct cpu_data cpus[ARCH_MAX_CPUS];
_Atomic int num_cpus_attempt_started = 1;
_Atomic int num_cpus_reported_started = 1;

export struct cpu_data* KeGetCpu(void) {
    return &cpus[ArchGetCpuNum()];
}

void KeInitCpuTable(void) {
    for (int i = 0; i < ARCH_MAX_CPUS; ++i) {
        cpus[i].cpu_num = i;
    }
    ArchInitPlatformSpecificData(&cpus[0]); 
}

export int KeRegisterNewCpu() {
    int idx = num_cpus_reported_started++;
    ArchInitPlatformSpecificData(&cpus[idx]);
    return idx;
}

export int KeGetNumActiveCpus() {
    return num_cpus_reported_started;
}
