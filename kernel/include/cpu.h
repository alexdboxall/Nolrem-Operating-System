#pragma once

#include <arch.h>

struct cpu_data {
    struct vas* current_vas;
    struct thread* current_thread;
    platform_cpu_data_t* platform_specific;
    int cpu_num;
};

void InitCpuTable(void);
struct cpu_data* GetCpu(void);