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
