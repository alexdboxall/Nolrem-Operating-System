#pragma once

#include <stddef.h>
#include <arch.h>

struct boot_memory_entry;

void KeInitPhys(struct boot_memory_entry* table, size_t count);

struct phys_page;

size_t KeAllocPhys(bool pin);
size_t KeAllocZeroedPhys(void);

bool KeProvideZeroedPhys(size_t phys);

void KeFreeDiscardedPhys(struct phys_page* pp);

struct phys_page* KeGetPhysPage(size_t addr);
size_t KeGetPhysAddr(struct phys_page* pp);
struct phys_page* KeGetFirstPhysPage(void);
struct phys_page* KeGetNextPhysPage(struct phys_page* pp);

size_t KeGetTotalMemory(void);
size_t KeGetFreeMemory(void);

bool KeIsMemoryRunningLow(void);      /* start thinking about tossing caches */
bool KeIsMemoryLow(void);             /* discarding now, toss everything possible */
bool KeIsOutOfMemory(void);         /* stop servicing any user allocs */
