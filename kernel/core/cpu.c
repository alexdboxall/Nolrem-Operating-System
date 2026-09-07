#include <stdint.h>
#include <string.h>
#include <log.h>
#include <common.h>
#include <cpu.h>
#include <arch.h>

struct vas;
struct thread;

static struct cpu_data cpus[ARCH_MAX_CPUS];
int num_cpus_started = 1;

struct cpu_data* GetCpu(void) {
    return &cpus[ArchGetCpuNum()];
}

void InitCpuTable(void) {
    for (int i = 0; i < ARCH_MAX_CPUS; ++i) {
        cpus[i].cpu_num = i;
    }
    ArchInitPlatformSpecificData(&cpus[0]); 
}