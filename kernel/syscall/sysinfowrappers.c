#include <common.h>
#include <syscall.h>
#include <usersys.h>

/*
 * User side only: these are QuerySystemInformation() with the type and the size
 * filled in, so there are no system calls of their own. On failure (which, with
 * a buffer on the stack, only happens if the kernel doesn't know the type) the
 * last error is set by QuerySystemInformation() and they return 0.
 */

export pageableuserexec int GetNumCpus(void) {
    uint32_t value = 0;
    if (!QuerySystemInformation(SYSINFO_CPU_COUNT, &value, sizeof(value))) {
        return 0;
    }
    return (int) value;
}

export pageableuserexec uint32_t GetCpuSpeed(void) {
    uint32_t value = 0;
    if (!QuerySystemInformation(SYSINFO_CPU_SPEED, &value, sizeof(value))) {
        return 0;
    }
    return value;
}

export pageableuserexec uint64_t GetPerformanceCounter(void) {
    uint64_t value = 0;
    if (!QuerySystemInformation(SYSINFO_PERFORMANCE_COUNTER, &value, sizeof(value))) {
        return 0;
    }
    return value;
}

export pageableuserexec uint64_t GetPerformanceFrequency(void) {
    uint64_t value = 0;
    if (!QuerySystemInformation(SYSINFO_PERFORMANCE_FREQUENCY, &value, sizeof(value))) {
        return 0;
    }
    return value;
}

export pageableuserexec bool GetMemoryInfo(struct memory_info* info) {
    return QuerySystemInformation(SYSINFO_MEMORY, info, sizeof(struct memory_info));
}
