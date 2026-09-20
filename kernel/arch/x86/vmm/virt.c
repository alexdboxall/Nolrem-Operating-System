#include <common.h>
#include <spinlock.h>
#include <heap.h>
#include <vmm.h>
#include <log.h>
#include <machine/x86.h>
#include <phys.h>
#include <string.h>
#include <arch.h>
#include <panic.h>

#define KRNL_TABLES_START   (ARCH_KRNL_MAPPING_BASE / (PAGE_SIZE * PAGE_SIZE / sizeof(size_t)))

#define RECURSIVE_BASE_TABLES           0xFFC00000

#define PAGE_PRESENT        (1ULL << 0) // Bit 0: Page is present in memory
#define PAGE_WRITE          (1ULL << 1) // Bit 1: Read/Write (1 = writable, 0 = read-only)
#define PAGE_USER           (1ULL << 2) // Bit 2: User/Supervisor (1 = user mode accessible)
#define PAGE_PWT            (1ULL << 3) // Bit 3: Page-level write-through caching
#define PAGE_PCD            (1ULL << 4) // Bit 4: Page-level cache disable
#define PAGE_ACCESSED       (1ULL << 5) // Bit 5: Set by CPU when read/written
#define PAGE_DIRTY          (1ULL << 6) // Bit 6: Set by CPU when written to
#define PAGE_SIZE_HUGE      (1ULL << 7) // Bit 7: 2MB/1GB page (or PAT at 4KB PTE level)
#define PAGE_GLOBAL         (1ULL << 8) // Bit 8: Global translation (don't flush on CR3 switch)


// The address of these symbols is the important thing, not the symbol
// value itself.
extern size_t boot_page_directory;
extern size_t boot_page_table1;

static size_t kernel_page_tables_phys[256] = {0};

static size_t scratch_virt_region;

static struct vas* kernel_vas;

static void Invalidate(size_t virt) {
    asm volatile("invlpg (%0)" : : "r"(virt) : "memory");
}

static size_t* GetRecursiveTable(size_t table_num) {
    return ((size_t*) RECURSIVE_BASE_TABLES) + (table_num * PAGE_SIZE / sizeof(size_t));
}

/* 
 * If it handled the page fault, returns true. If it has not handled it, and
 * needs the normal handler to run, returns false. 
 */
bool ArchTryHandleSpecialPageFault(size_t virt) {
    /* 
     * Check if this page fault was due to a new kernel page table being added,
     * but has not yet been lazy loaded into this VAS.
     */
    size_t table_idx = virt / (PAGE_SIZE * PAGE_SIZE / sizeof(size_t));
    if (table_idx < KRNL_TABLES_START) {
        return false;
    }
    size_t* cr3 = GetRecursiveTable(1023);
    if (!(cr3[table_idx] & PAGE_PRESENT) 
        && kernel_page_tables_phys[table_idx - KRNL_TABLES_START] != 0
    ) {
        cr3[table_idx] = kernel_page_tables_phys[table_idx - KRNL_TABLES_START] | PAGE_PRESENT | PAGE_WRITE;
        Invalidate((size_t) GetRecursiveTable(table_idx));
        return true;
    }
    return false;
}


void ArchSwitchToVas(struct vas* vas) {
    asm volatile ("mov %0, %%cr3" : : "r" (vas->arch_data));
}

static bool InKernelRange(size_t virt) {
    return virt >= ARCH_KRNL_MAPPING_BASE;
}

static size_t TranslateToEntry(struct virt_page* vp) {
    size_t phys = vp->phys & ~0xFFF;
    size_t flags = 0;
    
    flags |= (vp->present && !vp->busy) ? PAGE_PRESENT : 0;
    flags |= vp->write ? PAGE_WRITE : 0;
    flags |= vp->user ? PAGE_USER : 0;

    /* 
     * The global flag only exists on Pentium and later. But the i486 ignores
     * this bit, even though it's meant to be reserved as zero. So it's fine to
     * set.
     */
    flags |= InKernelRange(vp->virt) ? PAGE_GLOBAL : 0;
    
    return phys | flags;
}

void ArchReadVirtDirtyAndAccessed(struct virt_page* vp) {
    size_t virt_index = vp->virt / PAGE_SIZE;
    size_t level1_index = virt_index / (PAGE_SIZE / sizeof(size_t));
    size_t level2_index = virt_index % (PAGE_SIZE / sizeof(size_t));
    
    size_t* directory = GetRecursiveTable(1023);
    if (!(directory[level1_index] & PAGE_PRESENT)) {
        /* Can't be dirty or accessed if it doesn't exist. */
        return;
    }

    size_t* table = GetRecursiveTable(level1_index);
    size_t entry = table[level2_index];
    vp->dirty = !!(entry & PAGE_DIRTY);
    vp->accessed = !!(entry & PAGE_ACCESSED);
    *table &= ~(PAGE_DIRTY | PAGE_ACCESSED);
}

static void SetPte(struct vas* vas, size_t virt, size_t entry) {
    // TODO: do we need to check if this VAS is currently in? and if not, 
    //       temporarily map it in?
    // or does this only ever get called on the current vas?

    LogPrintf("Setting PTE: virt = 0x%X, entry = 0x%X", virt, entry);
    if (vas != GetCurrentVas()){ 
        LogString("SetPte called in non-current VAS!");
        Panic(PANIC_INVALID_ARCH_OPERATION);
    }

    size_t virt_index = virt / PAGE_SIZE;
    size_t level1_index = virt_index / (PAGE_SIZE / sizeof(size_t));
    size_t level2_index = virt_index % (PAGE_SIZE / sizeof(size_t));
    
    size_t* directory = GetRecursiveTable(1023);
    if (!(directory[level1_index] & PAGE_PRESENT)) {
        /* Time to map a new table. */
        LogPrintf("Allocating new phys for it...\n");
        size_t phys = AllocPhys(true);
        directory[level1_index] = phys | PAGE_PRESENT | PAGE_WRITE;
        size_t* table = GetRecursiveTable(level1_index);
        Invalidate((size_t) table);
        memset(table, 0, PAGE_SIZE);

        if (InKernelRange(virt)) {
            kernel_page_tables_phys[level1_index - ARCH_KRNL_MAPPING_BASE / (PAGE_SIZE * PAGE_SIZE / sizeof(size_t))] = phys;
        }
    }

    size_t* table = GetRecursiveTable(level1_index);
    LogPrintf("About to set the PTE...\n");
    LogPrintf("Old entry was 0x%X, new is 0x%X\n", table[level2_index], entry);
    table[level2_index] = entry;
    LogPrintf("Set!\n");
    // TODO: only needed if current VAS
    Invalidate(virt);
    LogPrintf("Invalidated!\n");
}

void ArchSyncVirt(struct vas* vas, struct virt_page* vp) {
    LogPrintf("ArchSyncVirt: 0x%X\n", vp->virt);
    SetPte(vas, vp->virt, TranslateToEntry(vp));
}

size_t ArchGetTemporaryPage(size_t phys) {
    size_t cpu_num = ArchGetCpuNum();
    LogPrintf("ArchGetTemporaryPage: 0x%X\n", scratch_virt_region + cpu_num * PAGE_SIZE);
    SetPte(GetCurrentVas(), scratch_virt_region + cpu_num * PAGE_SIZE, phys | PAGE_PRESENT | PAGE_WRITE);
    return scratch_virt_region + cpu_num * PAGE_SIZE;
}

void ArchReleaseTemporaryPage(size_t virt) {
    SetPte(GetCurrentVas(), virt, 0);
}

void ArchMapKernelPageDirectly(size_t phys, size_t virt) {
    SetPte(GetCurrentVas(), virt, (phys & ~0xFFF) | PAGE_PRESENT | PAGE_WRITE);
}

void ArchInitVas(struct vas* vas, bool first) {
    if (first) {
        kernel_page_tables_phys[0] = ((size_t) &boot_page_table1) - ARCH_KRNL_MAPPING_BASE;
        vas->arch_data = SubVoidPtr((void*) &boot_page_directory, ARCH_KRNL_MAPPING_BASE);
        kernel_vas = vas;

        /* Set up recursive mapping. */
        (&boot_page_directory)[1023] = ((size_t) vas->arch_data) | PAGE_PRESENT | PAGE_WRITE;
        
        extern char __start_kuser[];
        extern char __end_kuser[];
        extern char __start_pageablekuser[];
        extern char __end_pageablekuser[];

        size_t user_start_page = ((size_t) __start_kuser) / PAGE_SIZE;
        size_t user_end_page = (((size_t) __end_kuser) + PAGE_SIZE - 1) / PAGE_SIZE;
        size_t userpageable_start_page = ((size_t) __start_pageablekuser) / PAGE_SIZE;
        size_t userpageable_end_page = (((size_t) __end_pageablekuser) + PAGE_SIZE - 1) / PAGE_SIZE;
        
        /* 
         * For the user-accessible pages of the kernel, mark them as PAGE_USER,
         * but we also must make them read-only for the user (the kernel code
         * pages we don't *need* to mark as read-only, as the user can't get to
         * those anyway).
         */
        for (size_t i = user_start_page; i < user_end_page; ++i) {
            size_t* table = GetRecursiveTable(i / (PAGE_SIZE / sizeof(size_t)));
            table[i % (PAGE_SIZE / sizeof(size_t))] |= PAGE_USER;
            table[i % (PAGE_SIZE / sizeof(size_t))] &= ~PAGE_WRITE;
        }
        for (size_t i = userpageable_start_page; i < userpageable_end_page; ++i) {
            size_t* table = GetRecursiveTable(i / (PAGE_SIZE / sizeof(size_t)));
            table[i % (PAGE_SIZE / sizeof(size_t))] |= PAGE_USER;
            table[i % (PAGE_SIZE / sizeof(size_t))] &= ~PAGE_WRITE;
        }

        scratch_virt_region = AllocVirt(PAGE_SIZE * ARCH_MAX_CPUS);
        LogPrintf("Scratch virt region is at: 0x%X\n", scratch_virt_region);
        for (size_t i = 0; i < (size_t) ARCH_MAX_CPUS; ++i) {
            SetPte(vas, scratch_virt_region + i * PAGE_SIZE, 0);
        }
    
    } else {
        
    }

    /* Make the recursive mapping go live. */
    if (first) {
        ArchSwitchToVas(vas);
    }
}
