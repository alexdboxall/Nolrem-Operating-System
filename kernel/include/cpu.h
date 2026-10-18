#pragma once

#include <arch.h>

/* Force a power of 2 alignment so looking up CPU in CPU array doesn't need
 * multiply (as this lookup done on every spinlock acquire/release) */
struct cpu_data {
    struct vas* current_vas;
    struct thread* current_thread;
    platform_cpu_data_t* platform_specific;
    int cpu_num;
    uint32_t irq_disable_counter;
    bool saved_interrupt_enabled;
}  __attribute__((aligned(64)));

void InitCpuTable(void);
struct cpu_data* GetCpu(void);