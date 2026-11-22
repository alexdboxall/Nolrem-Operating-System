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

/*
 * How much would be free if every page KeDiscardPage() is able to discard were
 * discarded right now: KeGetFreeMemory() plus the discardable pages. Constant time.
 * It is an estimate, and can only be too high, never too low: a page written through its
 * mapping looks clean until the next time the VMM looks at it (see phys.c).
 */
size_t KeGetFreeMemoryIfDiscarded(void);

/* Whether KeDiscardPage() is able to discard the page (file backed, mapped, not wired, not dirty). Hold pp->lock. */
bool KeIsPageDiscardable(const struct phys_page* pp);

/*
 * Keeps the count behind KeGetFreeMemoryIfDiscarded() right. Call it, with pp->lock held, after
 * anything that can change whether the page is discardable: its wire count, its dirty flag, its
 * origin, or whether anything maps it. (vmm.c does this at the end of every such change.)
 */
void KeUpdateDiscardableCount(struct phys_page* pp);

/* Debug: re-derives the count from every page and checks it against the running one. False means a missed update. */
bool KeVerifyDiscardableCount(void);

bool KeIsMemoryRunningLow(void);      /* start thinking about tossing caches */
bool KeIsMemoryLow(void);             /* discarding now, toss everything possible */
bool KeIsOutOfMemory(void);         /* stop servicing any user allocs */
