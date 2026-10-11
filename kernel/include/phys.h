#pragma once

#include <stddef.h>
#include <arch.h>

struct boot_memory_entry;

void InitPhys(struct boot_memory_entry* table, size_t count);

struct phys_page;

size_t AllocPhys(bool pin);
void FreeDiscardedPhys(struct phys_page* pp);

struct phys_page* GetPhysPage(size_t addr);
size_t GetPhysAddr(struct phys_page* pp);
struct phys_page* GetFirstPhysPage(void);
struct phys_page* GetNextPhysPage(struct phys_page* pp);

size_t GetTotalMemory(void);
size_t GetFreeMemory(void);
