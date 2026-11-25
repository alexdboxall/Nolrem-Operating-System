#pragma once

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

/*
 * The kernel's own user mode calls that don't involve any object: yielding,
 * sleeping, the time, and information about the machine. (The ones for object
 * handles, Ref() and friends, are in uobj.h.) The system call numbers are in
 * syscall.h, and the implementations in syscall/.
 *
 * Calls that can fail set the thread's last error (see GetLastError()) and
 * return 0 (or false); the ones that cannot fail don't touch it.
 */

/*
 * Lets other threads run, and returns when the scheduler gets back to this one,
 * which may be straight away if nothing else is ready.
 */
void Yield(void);

/*
 * Sleeps for at least this long. A time of 0 is the same as Yield(). The time
 * is a minimum, and is rounded up to a tick of the system timer (40ms at the
 * moment).
 */
void SleepNano(uint64_t ns);
void SleepMilli(uint64_t ms);

/*
 * Sleeps until the given time since boot (see GetTimeSinceBoot()). Returns
 * straight away if that has already passed. Use this rather than SleepMilli()
 * in a loop that has to keep time, as it does not drift.
 */
void SleepUntil(uint64_t time_since_boot_ns);

/* Nanoseconds since the system started. Counts up at the rate of the system timer. */
uint64_t GetTimeSinceBoot(void);

/*
 * The current time in UTC, from the real time clock, in microseconds since the
 * 1st of January 1601 (see timeconv.h). There is no time zone setting yet, so the
 * clock is taken to hold UTC.
 */
uint64_t GetUtcTime(void);

/*
 * Information about the machine. `type` says which, and what is written to
 * `buffer` depends on it:
 *
 *      SYSINFO_CPU_COUNT               uint32_t        the number of CPUs that are running
 *      SYSINFO_CPU_SPEED               uint32_t        the speed of the CPU in kilohertz; on a
 *                                                      486 this is a rough estimate
 *      SYSINFO_PERFORMANCE_COUNTER     uint64_t        a counter for timing short things. It ticks
 *                                                      SYSINFO_PERFORMANCE_FREQUENCY times a
 *                                                      second, but is not a clock: it starts at an
 *                                                      arbitrary value, and different CPUs can
 *                                                      disagree about it
 *      SYSINFO_PERFORMANCE_FREQUENCY   uint64_t        ticks of the counter per second. A tick can
 *                                                      be a CPU cycle or a good deal longer, so
 *                                                      always use this rather than assuming
 *      SYSINFO_MEMORY                  struct memory_info
 *
 * `size` is the size of the buffer, which has to be at least as big as the
 * type's result (any extra is left alone). The exception is the structs, like
 * memory_info, which may get new fields on the end: they can be given a smaller
 * buffer, and fill in as much as will fit, so a program built before a field
 * was added carries on working.
 *
 * Returns true, or false with the last error set (EINVAL for a type that is not
 * known, ENOBUFS for a buffer that is too small, or the error for a pointer that
 * can't be written to). Nothing is written when it fails. New types will be
 * added over time, so ask for one at a time and don't assume that a type this
 * header doesn't list is invalid on a newer kernel.
 */
#define SYSINFO_CPU_COUNT               1
#define SYSINFO_CPU_SPEED               2
#define SYSINFO_PERFORMANCE_COUNTER     3
#define SYSINFO_PERFORMANCE_FREQUENCY   4
#define SYSINFO_MEMORY                  5

/*
 * All sizes are in bytes. `available` is `free` plus the memory that could be
 * freed straight away by dropping clean pages of files (it is a little
 * optimistic, as a page written to doesn't show up as changed straight away).
 */
struct memory_info {
    uint64_t total;
    uint64_t free;
    uint64_t available;
    uint64_t page_size;
};

bool QuerySystemInformation(int type, void* buffer, size_t size);

/*
 * The usual ones, so that nobody has to write out the type and the size each
 * time. They are only QuerySystemInformation() underneath. The ones that give a
 * number return 0 if the query fails (and set the last error), which for a
 * buffer on the stack means the kernel doesn't know the type; a counter that
 * returns 0 can't be told from one that has failed, so check GetLastError()
 * if it matters.
 */
int GetNumCpus(void);                       /* SYSINFO_CPU_COUNT */
uint32_t GetCpuSpeed(void);                 /* SYSINFO_CPU_SPEED, in kilohertz */
uint64_t GetPerformanceCounter(void);       /* SYSINFO_PERFORMANCE_COUNTER */
uint64_t GetPerformanceFrequency(void);     /* SYSINFO_PERFORMANCE_FREQUENCY, in ticks per second */
bool GetMemoryInfo(struct memory_info* info);   /* SYSINFO_MEMORY */

/*
 * The error from the last call to fail on this thread, as an errno value. Calls
 * that succeed leave it alone, so only look at it after one has failed.
 */
int GetLastError(void);
void SetLastError(int error);
