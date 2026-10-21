#pragma once

#include <stddef.h>
#include <arch.h>

struct boot_memory_entry;

void InitPhys(struct boot_memory_entry* table, size_t count);

struct phys_page;

size_t AllocPhys(bool pin);
size_t AllocZeroedPhys(void);

bool ProvideZeroedPhys(size_t phys);

void FreeDiscardedPhys(struct phys_page* pp);

struct phys_page* GetPhysPage(size_t addr);
size_t GetPhysAddr(struct phys_page* pp);
struct phys_page* GetFirstPhysPage(void);
struct phys_page* GetNextPhysPage(struct phys_page* pp);

size_t GetTotalMemory(void);
size_t GetFreeMemory(void);

bool IsMemoryRunningLow(void);      /* start thinking about tossing caches */
bool IsMemoryLow(void);             /* discarding now, toss everything possible */
