#pragma once

#include <machine/config.h>
#include <common.h>

void ArchIndicateSpinLoop(void);
void ArchIdle(void);

void ArchInit(void);

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

void ArchCallGlobalConstructors();


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
size_t ArchGetTemporaryPage(size_t phys);
void ArchReleaseTemporaryPage(size_t virt);
int ArchGetCpuNum(void);

struct cpu_data;

void ArchInitPlatformSpecificData(struct cpu_data* cpu);

struct thread;
void ArchSwitchThread(struct thread* old_thr, struct thread* new_thr);
void ArchSetupNewThreadEntry(struct thread* thr, void(*entry)(void*), void*);
