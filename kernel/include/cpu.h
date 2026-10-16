#pragma once

#include <arch.h>

struct cpu_data {
    struct vas* current_vas;
    struct thread* current_thread;
    platform_cpu_data_t* platform_specific;
    int cpu_num;
    uint32_t irq_disable_counter;
    bool saved_interrupt_enabled;
};

void InitCpuTable(void);
struct cpu_data* GetCpu(void);