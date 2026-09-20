#include <phys.h>
#include <bootloader.h>
#include <common.h>
#include <log.h>
#include <string.h>
#include <vmm.h>
#include <scheduler.h>
#include <arch.h>

/* At or below this number of physical pages, we start discarding. */
#define LOW_MEM_THRESHOLD   16

static struct phys_page* sys_pp_table;
static _Atomic size_t sys_total_pp;
static _Atomic size_t sys_free_pp;
static size_t sys_pp_table_max_index;

struct phys_page* GetPhysPage(size_t addr) {
    return sys_pp_table + (addr / PAGE_SIZE);
}

size_t GetPhysAddr(struct phys_page* pp) {
    return ((size_t) (pp - sys_pp_table)) * PAGE_SIZE;
}

struct phys_page* GetFirstPhysPage(void) {
    return sys_pp_table;
}

struct phys_page* GetNextPhysPage(struct phys_page* pp) {
    struct phys_page* last_valid = sys_pp_table + sys_pp_table_max_index;
    if (pp == last_valid) {
        return NULL;
    } else {
        return pp + 1;
    }
}

export size_t GetTotalMemory(void) {
    return sys_total_pp * PAGE_SIZE;
}

export size_t GetFreeMemory(void) {
    return sys_free_pp * PAGE_SIZE;
}

void FreeDiscardedPhys(struct phys_page* pp) {
    /*
     * Reset ALL the state, not just 'allocated'. A frame that had been dirtied
     * once came back from the allocator still marked dirty, and since
     * FindDiscardPage() requires !dirty it was permanently unreclaimable.
     */
    pp->chain = NULL;
    pp->origin = NULL;
    pp->allocated = 0;
    pp->dirty = 0;
    pp->wired = 0;
    pp->lru = 0;
    ++sys_free_pp;
}

export size_t AllocPhys(bool pin) {
    struct phys_page* curr = GetFirstPhysPage();
    while (curr != NULL) {
        bool acquired = TryAcquireSpinlock(&curr->lock);
        if (!acquired) {
            curr = GetNextPhysPage(curr);
            continue;
        }
        if (curr->exists && !curr->allocated) {
            curr->allocated = 1;
            curr->wired = pin;
            if (sys_free_pp <= LOW_MEM_THRESHOLD) {
                LogPrintf("SENDING LOW MEM\n");
                PostMessageIrq((struct msg) {
                    .type = SYSMSG_LOWMEMORY
                });
            }
            --sys_free_pp;
            ReleaseSpinlock(&curr->lock);
            return GetPhysAddr(curr);
        }
        ReleaseSpinlock(&curr->lock);
        curr = GetNextPhysPage(curr);
    }
    return 0;
}

/* Not needed, as the kernel task continues to use the bootstrap one anyway?*/
void ReclaimBootstrapStackPhys(void) {
    /*extern size_t stack_bottom;
    extern size_t stack_top;

    size_t start_page = (((size_t) &stack_bottom) + PAGE_SIZE - 1) / PAGE_SIZE;
    size_t end_page = ((size_t) &stack_top) / PAGE_SIZE;

    for (size_t i = start_page; i < end_page; ++i) {
        LogPrintf("Reclaiming 0x%X...\n", i * PAGE_SIZE);
        ++sys_total_pp;
        ++sys_free_pp;
        sys_pp_table[i - ARCH_KRNL_MAPPING_BASE / PAGE_SIZE].exists = true;
    }


    while (true) {
        ;
    }
*/

    LogPrintf("Total RAM: %dKB\n", sys_total_pp * PAGE_SIZE / 1024);
    LogPrintf("Free  RAM: %dKB\n", sys_free_pp  * PAGE_SIZE / 1024);
}

void InitPhys(struct boot_memory_entry* table, size_t count) {
    LogString("\nInit physical memory... ");
    size_t total_phys_pages = 0;
    size_t max_phys_page_idx = 0;

    for (size_t i = 0; i < count; ++i) {
        struct boot_memory_entry entry = table[i];
        size_t page_start = (entry.address + PAGE_SIZE - 1) / PAGE_SIZE;
        size_t page_end = (entry.address + entry.length) / PAGE_SIZE;
    
        if (BOOTRAM_GET_TYPE(entry.info) == BOOTRAM_TYPE_AVAILABLE) {         
            total_phys_pages += page_end - page_start;
            max_phys_page_idx = MAX(max_phys_page_idx, page_end);
        }
    }

    extern size_t _kernel_end;
    size_t max_id_mapped = 1024 * 4096;
	size_t max_kernel_addr = (((size_t) &_kernel_end) - 0xC0000000 + 0xFFF) & ~0xFFF;
    size_t max_pp = (max_id_mapped - max_kernel_addr) / sizeof(struct phys_page);

    sys_pp_table = (struct phys_page*) (0xC0000000 + max_kernel_addr);
    sys_pp_table_max_index = MIN(max_phys_page_idx, max_pp) - 1;

    size_t max_addr_used_now = max_kernel_addr
            + ((sys_pp_table_max_index * sizeof(struct phys_page) + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1)); 

    for (size_t i = 0; i < sys_pp_table_max_index; ++i) {
        memset(&sys_pp_table[i], 0, sizeof(struct phys_page));
        InitSpinlock(&sys_pp_table[i].lock);
    }

    for (size_t i = 0; i < count; ++i) {
        struct boot_memory_entry entry = table[i];
        size_t page_start = (entry.address + PAGE_SIZE - 1) / PAGE_SIZE;
        size_t page_end = (entry.address + entry.length) / PAGE_SIZE;
    
        if (BOOTRAM_GET_TYPE(entry.info) == BOOTRAM_TYPE_AVAILABLE) {     
            sys_total_pp += page_end - page_start;
    
            for (size_t j = page_start; j < page_end; ++j) {
                if (j * PAGE_SIZE >= 0x10000 && j * PAGE_SIZE < max_addr_used_now) {
                    continue;
                }
                if (j * PAGE_SIZE == 0x0000 || j * PAGE_SIZE == 0xA000 || j * PAGE_SIZE == 0xC000) {
                    // 0 has IVT/BIOS stuff
                    // 0xA000 has the kernel boot info table
                    // 0xC000 has the RAM data in it
                    
                    // TODO: a more stable way of getting this info!!
                    continue;
                }
                if (j < sys_pp_table_max_index) {
                    ++sys_free_pp;
                    sys_pp_table[j].exists = true;
                }
            }
        }
    }

    LogPrintf("Total RAM: %dKB\n", sys_total_pp * PAGE_SIZE / 1024);
    LogPrintf("Free  RAM: %dKB\n", sys_free_pp  * PAGE_SIZE / 1024);
}

