#include <common.h>
#include <arch.h>
#include <cpu.h>
#include <phys.h>
#include <string.h>
#include <transfer.h>
#include <syscall.h>
#include <usersys.h>

/* Every type's answer is built here and then copied out, so the user's buffer is
 * only touched by the one transfer. */
union sysinfo_data {
    uint32_t u32;
    uint64_t u64;
    struct memory_info memory;
};

static void KeSysinfoCpuCount(union sysinfo_data* data) {
    data->u32 = (uint32_t) KeGetNumActiveCpus();
}

static void KeSysinfoCpuSpeed(union sysinfo_data* data) {
    data->u32 = ArchGetCpuSpeed();
}

static void KeSysinfoPerformanceCounter(union sysinfo_data* data) {
    data->u64 = ArchGetPerformanceCounter();
}

static void KeSysinfoPerformanceFrequency(union sysinfo_data* data) {
    data->u64 = ArchGetPerformanceCounterFrequency();
}

static void KeSysinfoMemory(union sysinfo_data* data) {
    data->memory.total = KeGetTotalMemory();
    data->memory.free = KeGetFreeMemory();
    data->memory.available = KeGetFreeMemoryIfDiscarded();
    data->memory.page_size = PAGE_SIZE;
}

/*
 * One row per type. To add a type, put it here and in usersys.h (and in the
 * union above if it needs a new kind of result), and nothing else changes.
 *
 * `extensible` types are structs that may get new fields on the end. They can be
 * asked for with a buffer that is smaller than the current struct (as a program
 * built before the new field was added will), and the kernel fills in as much as
 * there is room for. For any other type the buffer must be big enough.
 */
struct sysinfo_type {
    size_t size;
    bool extensible;
    void (*fill)(union sysinfo_data*);
};

static const struct sysinfo_type sysinfo_types[] = {
    [SYSINFO_CPU_COUNT]             = { sizeof(uint32_t),           false, KeSysinfoCpuCount },
    [SYSINFO_CPU_SPEED]             = { sizeof(uint32_t),           false, KeSysinfoCpuSpeed },
    [SYSINFO_PERFORMANCE_COUNTER]   = { sizeof(uint64_t),           false, KeSysinfoPerformanceCounter },
    [SYSINFO_PERFORMANCE_FREQUENCY] = { sizeof(uint64_t),           false, KeSysinfoPerformanceFrequency },
    [SYSINFO_MEMORY]                = { sizeof(struct memory_info), true,  KeSysinfoMemory },
};

#define NUM_SYSINFO_TYPES   (sizeof(sysinfo_types) / sizeof(sysinfo_types[0]))

static_assert(sizeof(union sysinfo_data) >= sizeof(struct memory_info), "union too small");

/*
 * EINVAL for a type that doesn't exist, ENOBUFS for a buffer that is too small.
 * (The size is checked before anything is written, so a failure writes nothing.)
 */
pageable size_t SysQuerySystemInformation(size_t type, size_t out_ptr, size_t out_size, size_t) {
    if (type == 0 || type >= NUM_SYSINFO_TYPES || sysinfo_types[type].fill == NULL) {
        return EINVAL;
    }
    const struct sysinfo_type* info = &sysinfo_types[type];

    if (out_size == 0) {
        return EINVAL;
    }
    if (out_size < info->size && !info->extensible) {
        return ENOBUFS;
    }

    union sysinfo_data data;
    memset(&data, 0, sizeof(data));
    info->fill(&data);

    struct transfer tr = KeCreateTransferWritingToUser((void*) out_ptr, out_size, 0);
    return KePerformTransfer(&data, &tr, info->size);   /* copies no more than fits */
}

export pageableuserexec bool QuerySystemInformation(int type, void* buffer, size_t size) {
    size_t res = SystemCall4(SYS_QuerySystemInformation, (size_t) type, (size_t) buffer, size);
    if (res != 0) {
        SetLastError((int) res);
        return false;
    }
    return true;
}
