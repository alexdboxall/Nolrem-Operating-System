#pragma once

/*
 * Low memory policy.
 *
 * When free physical memory runs low, the physical allocator queues a
 * SYSMSG_LOWMEMORY on the system message box. Whoever drains that box (the
 * window manager's main loop) responds by calling KeReclaimMemory(), which is
 * the one place that decides what gets given back, and in what order:
 *
 *   1. discard a couple of pages
 *   2. run every registered callback (components flushing their own caches)
 *   3. evict unreferenced vnodes
 *   4. return empty areas of the heap
 *
 * Components that cache memory they could do without register a callback with
 * KeRegisterReclaimCallback() once at startup. Callbacks run in thread context,
 * must not assume any particular lock is held, and should only free memory.
 */

typedef void (*ke_reclaim_callback_t)(void);

/* Returns 0, or ENOSPC if the (small, fixed) callback table is full. */
int KeRegisterReclaimCallback(ke_reclaim_callback_t callback);

void KeReclaimMemory(void);
