#pragma once

#include <machine/config.h>
#include <common.h>

/*
 * The architecture interface.
 *
 * Every function named Arch...() is part of the generic interface between the
 * architecture-independent kernel and the architecture it is built for. Each
 * architecture must provide all of them, and they are all declared in this file
 * (so if you need a new one, add it here).
 *
 * Anything that is internal to an architecture is not called Arch...(), but is
 * instead named after that architecture (x86...() for x86), and is declared in
 * that architecture's own headers (e.g. arch/x86/include/x86.h) or kept static
 * in its source file. Code outside arch/ must not call those.
 */

/* ---- Startup, CPU and idling ------------------------------------------------ */

void ArchIndicateSpinLoop(void);
void ArchIdle(void);

void ArchInit(void);    // very early on
void ArchInit2(void);   // after VFS, VMM, scheduler, etc. active

// machine/config needs to do a typedef 'irqcontext_t' 

// machine needs to define these
//      #define ARCH_KRNL_VIRT_RANGE_BASE 
//      #define ARCH_KRNL_VIRT_RANGE_BYTES
//      #define ARCH_USER_AREA_BASE
//      #define ARCH_USER_AREA_LIMIT
//      #define ARCH_KRNL_MAPPING_BASE
//      #define ARCH_MAX_CPUS
//      typedef ... platform_cpu_data_t 
/*
* Non-inclusive of ARCH_USER_AREA_LIMIT
*/

/* ---- Time ------------------------------------------------------------------- */

/*
 * We give `timezone_offset` in case the architecture/configuration wants to use
 * local time to store the RTC.
 * 
 * Microseconds since 1601.
 * 
 * local = UTC + timezone_offset
 * 
 * `ArchSetUtcTime` returns an errno.
 */
export uint64_t ArchGetUtcTime(int64_t timezone_offset);
export int ArchSetUtcTime(uint64_t time, int64_t timezone_offset);

/*
 * A fast counter, for timing short things. It only ever counts up, and it
 * ticks ArchGetPerformanceCounterFrequency() times a second.
 *
 * What it counts is up to the architecture: the x86 build uses the CPU's cycle
 * counter where there is one (so a tick is a cycle), and otherwise the best it
 * can do, which is the 8254 timer (about 0.84us per tick). So never assume a
 * tick length; always divide by the frequency.
 *
 * It is not comparable between CPUs, as they do not necessarily agree on the
 * count, and it is not a clock: it starts at an arbitrary value. Use
 * KeGetTimeSinceBoot() for time.
 */
export uint64_t ArchGetPerformanceCounter(void);

/* Ticks of ArchGetPerformanceCounter() per second. */
export uint64_t ArchGetPerformanceCounterFrequency(void);

/*
 * The speed of the CPU, in kilohertz, measured at boot. Accurate to well under
 * a percent when the CPU has a cycle counter. Without one (i486) it is a rough
 * estimate that assumes a typical CPU, and it is rounded to whole megahertz.
 */
export uint32_t ArchGetCpuSpeed(void);

void ArchCallGlobalConstructors();


/* ---- Virtual memory --------------------------------------------------------- */

struct virt_page;
struct vas;

void ArchInitVas(struct vas* vas, bool first);
void ArchSyncVirt(struct vas* vas, struct virt_page* vp);
void ArchSwitchToVas(struct vas* vas);
bool ArchTryHandleSpecialPageFault(size_t virt);

/* must also clear them */
void ArchReadVirtDirtyAndAccessed(struct virt_page* vp);

size_t ArchLockToCpu(void);
void ArchUnlockFromCpu(size_t rv);
void ArchMapKernelPageDirectly(size_t phys, size_t virt);
size_t ArchGetTemporaryPage(size_t phys);
void ArchReleaseTemporaryPage(size_t virt);

/* ---- CPUs and interrupts ---------------------------------------------------- */

int ArchGetCpuNum(void);

bool ArchAreInterruptsEnabled(void);
void ArchEnableInterrupts(void);
void ArchDisableInterrupts(void);

struct cpu_data;

void ArchInitPlatformSpecificData(struct cpu_data* cpu);

/* ---- Threads ---------------------------------------------------------------- */

struct thread;
void ArchSwitchThread(struct thread* old_thr, struct thread* new_thr);
void ArchSetupNewThreadEntry(struct thread* thr, void(*entry)(void*), void*);

/* ---- Debugging -------------------------------------------------------------- */

/* If true, the hardware timer will not call KeAdvanceTimer */
void ArchDebugInhibitTimerAdvance(bool inhibit);

/* ---- Modules ---------------------------------------------------------------- */

struct module;
struct file;
struct symbol_list* ArchLoadSymbols(struct file* file, size_t adjust, bool use_hash);
