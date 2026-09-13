#include <obj.h>
#include <spinlock.h>
#include <common.h>
#include <mutex.h>
#include <arch.h>
#include <string.h>
#include <vmm.h>
#include <errno.h>
#include <phys.h>
#include <allocvirt.h>
#include <panic.h>
#include <assert.h>
#include <log.h>
#include <heapex.h>
#include <heap.h>

/*
 * A fault should converge in a couple of trips at most. Anything beyond this
 * means a retry path exists that can never make progress, which used to hang
 * the machine in silence. Fail loudly instead.
 */
#define VMM_MAX_FAULT_RETRIES   1000

static struct page_origin* CreatePageOrigin(struct file* file, size_t file_offset,
                                            size_t base, size_t phys, bool fixed);

void LockVas(struct vas* vas) {
    LogPrintf("locking vas 0x%X\n", vas);
    AcquireMutex(vas->lock, TIMEOUT_INFINITE);
}

void UnlockVas(struct vas* vas) {
    LogPrintf("unlocking vas 0x%X\n", vas);
    ReleaseMutex(vas->lock);
}

/* 
 * Use the VMM heap for anything that needs to be done at 'AllocHeap' time.
 * i.e. everything EXCEPT for allocating new VAS, and allocating new VA.
 * (Which are done at process allocation, not memory request time).
 * 
 * So use VMM heap for: virt_page, page_origin, links/chains, mappings
 */
struct heap vmm_special_heap;
static uint8_t vmm_heap_bootstrap_page[PAGE_SIZE];
static bool used_vmm_bootstrap_page = false;

void* VmmSpecialHeapGetMemory(size_t* bytes) {
    LogPrintf("VmmSpecialHeapGetMemory: %d\n", *bytes);
    if (!used_vmm_bootstrap_page) {
        if (*bytes > PAGE_SIZE) {
            Panic(PANIC_VMM_SPECIAL_HEAP_BOOTSTRAPPED_WRONGLY);
        }
        used_vmm_bootstrap_page = true;
        LogPrintf("Bootstrap: 0x%X\n", vmm_heap_bootstrap_page);
        *bytes = PAGE_SIZE;
        return vmm_heap_bootstrap_page;
    } else {
        size_t virt = AllocVirt(*bytes);
        size_t pages = (*bytes + PAGE_SIZE - 1) / PAGE_SIZE;
        LogPrintf("Real deal: virt = 0x%X, pages = %d\n", virt, pages);
        for (size_t i = 0; i < pages; ++i) {
            ArchMapKernelPageDirectly(AllocPhys(true), virt + i * PAGE_SIZE); 
        }
        *bytes = pages * PAGE_SIZE;
        return (void*) virt;
    }
}

struct spinlock vmm_heap_lock;
void* AllocVmmHeap(size_t bytes) {
    LogPrintf("AllocVmmHeap(%d)\n", bytes);
    AcquireSpinlock(&vmm_heap_lock);
    void* retv = AllocHeapEx(&vmm_special_heap, bytes);
    ReleaseSpinlock(&vmm_heap_lock);
    return retv;
}

void FreeVmmHeap(void* ptr) {
    AcquireSpinlock(&vmm_heap_lock);
    FreeHeapEx(&vmm_special_heap, ptr);
    ReleaseSpinlock(&vmm_heap_lock);
}

/*
 * Returns the slot in the mapping tables that owns 'virt', or NULL if it is
 * unreachable (out of range, or the intermediate tables don't exist and we
 * weren't asked to create them, or creating them failed).
 *
 * Caller must hold vas->lock.
 */
static struct virt_page** GetVirtualPageSlot(struct vas* vas, size_t virt, bool create) {
    size_t index = virt / PAGE_SIZE;

    size_t level_3 = index % MAPPINGS_PER_LEVEL;
    index /= MAPPINGS_PER_LEVEL;
    size_t level_2 = index % MAPPINGS_PER_LEVEL;
    index /= MAPPINGS_PER_LEVEL;
    size_t level_1 = index;

    /*
     * Three levels of 128 covers 8GB. On 32-bit level_1 can only reach 63, so
     * this never fires; on 64-bit it absolutely would, and used to be a silent
     * out-of-bounds read straight off the end of vas->mappings.
     */
    if (level_1 >= MAPPINGS_PER_LEVEL) {
        return NULL;
    }

    struct virt_page*** table = vas->mappings[level_1];
    if (table == NULL) {
        if (!create) {
            return NULL;
        }
        table = AllocVmmHeap(MAPPINGS_PER_LEVEL * sizeof(struct virt_page**));
        if (table == NULL) {
            return NULL;
        }
        memset(table, 0, MAPPINGS_PER_LEVEL * sizeof(struct virt_page**));
        vas->mappings[level_1] = table;
    }

    struct virt_page** table2 = table[level_2];
    if (table2 == NULL) {
        if (!create) {
            return NULL;
        }
        table2 = AllocVmmHeap(MAPPINGS_PER_LEVEL * sizeof(struct virt_page*));
        if (table2 == NULL) {
            return NULL;
        }
        memset(table2, 0, MAPPINGS_PER_LEVEL * sizeof(struct virt_page*));
        table[level_2] = table2;
    }

    return &table2[level_3];
}

struct virt_page* GetVirtualPageFromVirt(struct vas* vas, size_t virt) {
    struct virt_page** slot = GetVirtualPageSlot(vas, virt, false);
    return slot == NULL ? NULL : *slot;
}

static struct vas* current_vas = NULL;
static struct vas* kernel_vas = NULL;

export struct vas* GetKernelVas(void) {
    return current_vas == NULL ? NULL : kernel_vas;
}

export struct vas* GetCurrentVas(void) {
    return current_vas;
}

static bool virt_initialised = false;

bool IsVirtInitialised(void) {
    return virt_initialised;
}

export struct vas* CreateVas(void) {
    struct vas* vas = AllocHeap(sizeof(struct vas));
    if (vas == NULL) {
        return NULL;
    }
    InitObject(vas, OBJTYPE_VAS);

    vas->lock = CreateMutex();
    vas->va = AllocHeap(sizeof(struct virt_arena));
    vas->mappings = AllocVmmHeap(MAPPINGS_PER_LEVEL * sizeof(struct virt_page***));
    if (vas->lock == NULL || vas->va == NULL || vas->mappings == NULL) {
        if (vas->mappings != NULL) FreeVmmHeap(vas->mappings);
        if (vas->va != NULL)       FreeHeap(vas->va);
        if (vas->lock != NULL)     DerefObject(vas->lock);
        FreeHeap(vas);
        return NULL;
    }

    InitVirtArena(vas->va, ARCH_USER_AREA_BASE, ARCH_USER_AREA_LIMIT);
    memset(vas->mappings, 0, MAPPINGS_PER_LEVEL * sizeof(struct virt_page***));

    ArchInitVas(vas, false);
    return vas;
}

void CreateInitialVas(void) {
    struct vas* vas = AllocHeap(sizeof(struct vas));
    assert(vas != NULL);
    InitObject(vas, OBJTYPE_VAS);

    vas->va = GetKernelVirtArena();
    vas->lock = CreateMutex();
    vas->mappings = AllocVmmHeap(MAPPINGS_PER_LEVEL * sizeof(struct virt_page***));
    assert(vas->lock != NULL && vas->mappings != NULL);
    memset(vas->mappings, 0, MAPPINGS_PER_LEVEL * sizeof(struct virt_page***));

    kernel_vas = vas;
    current_vas = vas;
    ArchInitVas(vas, true);

    virt_initialised = true;
}

static struct page_origin* CreatePageOrigin(struct file* file, size_t file_offset,
                                            size_t base, size_t phys, bool fixed) {
                                                    LogPrintf("a");

    struct page_origin* po = AllocVmmHeap(sizeof(struct page_origin));
    if (po == NULL) {
        return NULL;
    }
    InitStaticMutex(&po->mtx);
    InitObject(po, OBJTYPE_PAGE_ORIGIN);
    po->rebase_page = base;
    po->file = file;
    po->phys = phys;
    po->file_offset = file_offset;
    po->fixed = fixed;

    if (file != NULL) {
        RefObject(file);
    }
    return po;
}

static void MarkKernelPageDiscardable(size_t existing_virt, size_t existing_phys, bool allow_user, struct file* file, size_t file_offset) {
    struct virt_page* vp = AllocVmmHeap(sizeof(struct virt_page));
    struct vas* vas = GetKernelVas();
    InitObject(vp, OBJTYPE_PAGE_VIRT);
    vp->vas = vas;
    vp->virt = existing_virt;
    vp->executable = 1;
    vp->write = false;
    vp->user = allow_user;
    vp->accessed = 0;
    vp->busy = 0;
    vp->dirty = 0;
    vp->present = 1;
    vp->phys = existing_phys;

    struct phys_page* pp = GetPhysPage(existing_phys);
    pp->vp = vp;
    pp->vas = vas;
    pp->chain = NULL;

    LogPrintf("Page VIRT=0x%X, PHYS=0x%X is now discardable", existing_virt, existing_phys);
    RefObject(vp);
    RefObject(vas);
    pp->origin = CreatePageOrigin(file, file_offset, 0, existing_phys, false);
    LockVas(vp->vas);
    struct virt_page** slot = GetVirtualPageSlot(vas, existing_virt, true);
    if (slot == NULL || *slot != NULL) {
        Panic(PANIC_FUCK_ME);
    }
    *slot = vp;
    UnlockVas(vp->vas);
    LogPrintf(".\n");
}

void MarkPageableSegmentsDiscardable(void) {
    (void) MarkKernelPageDiscardable;

    extern char __start_pageable[];
    extern char __end_pageable[];
    extern char __start_pageablekuser[];
    extern char __end_pageablekuser[];

    size_t k_start_page = ((size_t) __start_pageable) / PAGE_SIZE;
    size_t k_end_page   = (((size_t) __end_pageable) + PAGE_SIZE - 1) / PAGE_SIZE;
    size_t u_start_page = ((size_t) __start_pageablekuser) / PAGE_SIZE;
    size_t u_end_page   = (((size_t) __end_pageablekuser) + PAGE_SIZE - 1) / PAGE_SIZE;

    struct obj_header* dummy_file = AllocHeap(sizeof(struct obj_header));
    InitObject(&dummy_file, OBJTYPE_FILE);
    LogPrintf("The dummy file is at: 0x%X\n", dummy_file);
    for (size_t i = k_start_page; i < k_end_page; ++i) {
        // TODO: need a real file! And offset!
        MarkKernelPageDiscardable(i * PAGE_SIZE, i * PAGE_SIZE - ARCH_KRNL_MAPPING_BASE, false, (void*) dummy_file, 0xCAFEBABE);
    }
    for (size_t i = u_start_page; i < u_end_page; ++i) {
        // TODO: need a real file! And offset!
        MarkKernelPageDiscardable(i * PAGE_SIZE, i * PAGE_SIZE - ARCH_KRNL_MAPPING_BASE, true, (void*) dummy_file, 0xCAFEBABE);
    } 

    LogPrintf("Going to discard a page...\n");
    DiscardPage();
}

/*
 * NOTE: 'fixed' is new. A fixed mapping carries a valid physical address from
 * birth, has no backing file, and must never be routed through the phys_page
 * dedup machinery. Previously AllocFixedMemory() produced an origin that the
 * fault handler could never satisfy, and it spun on 'goto retry' forever.
 */
export struct virt_page* CreateVirtPage(struct vas* vas, size_t virt, int flags,
                                        struct file* file, size_t file_offset,
                                        size_t base, size_t phys, bool fixed) {
    struct virt_page* vp = AllocVmmHeap(sizeof(struct virt_page));
    if (vp == NULL) {
        return NULL;
    }

    struct page_origin* po = CreatePageOrigin(file, file_offset, base, phys, fixed);
    if (po == NULL) {
        FreeVmmHeap(vp);
        return NULL;
    }

    InitObject(vp, OBJTYPE_PAGE_VIRT);

    vp->vas = vas;              /* back-pointer, deliberately unreferenced */
    vp->virt = virt;

    vp->executable = !!(flags & VP_EXEC);
    vp->write = !!(flags & VP_WRITE);
    vp->user = !!(flags & VP_USER);

    vp->accessed = 0;
    vp->busy = 0;
    vp->dirty = 0;
    vp->present = 0;
    vp->origin = po;            /* aliases vp->phys - set one or the other */

    LockVas(vas);

    struct virt_page** slot = GetVirtualPageSlot(vas, virt, true);

    if (slot == NULL || *slot != NULL) {
        /* Unmappable address, out of heap, or something is already here. */
        UnlockVas(vas);
        DerefObject(vp);        /* CleanupVirtPage drops the origin for us */
        return NULL;
    }
    *slot = vp;
    UnlockVas(vas);
    return vp;
}

/*
 * Rips 'count' pages starting at 'base' back out of the mapping tables. Only
 * safe on pages that have never been mapped in - i.e. the partial-failure
 * unwind below. A real unmap path has to evict from the phys_page first.
 */
static void UnwindVirtPages(struct vas* vas, size_t base, size_t count) {
    LockVas(vas);
    for (size_t i = 0; i < count; ++i) {
        struct virt_page** slot = GetVirtualPageSlot(vas, base + i * PAGE_SIZE, false);
        if (slot == NULL || *slot == NULL) {
            continue;
        }
        struct virt_page* vp = *slot;
        assert(!vp->present && vp->busy == 0);
        *slot = NULL;
        DerefObject(vp);
    }
    UnlockVas(vas);
}

/*
 * The Alloc*Memory() family used to reserve 'bytes' of virtual space but only
 * ever create a single virt_page. Everything past the first page faulted into
 * a NULL lookup.
 */
static void* AllocMemoryRange(struct file* file, size_t file_offset, size_t phys,
                              size_t bytes, int flags, size_t reloc_base, bool fixed) {
    if (bytes == 0) {
        return NULL;
    }

    struct vas* vas = GetCurrentVas();
    size_t pages = (bytes + PAGE_SIZE - 1) / PAGE_SIZE;

    size_t base = AllocVirt(pages * PAGE_SIZE);
    if (base == 0) {
        return NULL;
    }

    for (size_t i = 0; i < pages; ++i) {
        LogPrintf("!");
        struct virt_page* vp = CreateVirtPage(
            vas,
            base + i * PAGE_SIZE,
            flags,
            file,
            file != NULL ? file_offset + i * PAGE_SIZE : 0,
            reloc_base,
            phys != 0 ? phys + i * PAGE_SIZE : 0,
            fixed
        );
        LogPrintf("$ vp = 0x%X\n", vp);
        if (vp == NULL) {
            UnwindVirtPages(vas, base, i);
            FreeVirt(base, pages * PAGE_SIZE);
            return NULL;
        }
    }

    LogPrintf("Allocated anon memory at 0x%X\n", base);
    return (void*) base;
}

export void* AllocFixedMemory(size_t phys, size_t bytes, int flags) {
    return AllocMemoryRange(NULL, 0, phys, bytes, flags, 0, true);
}

export void* AllocAnonMemory(size_t bytes, int flags) {
    return AllocMemoryRange(NULL, 0, 0, bytes, flags, 0, false);
}

export void* AllocFileMemory(struct file* file, size_t file_offset, size_t bytes,
                             int flags, size_t reloc_base) {
    return AllocMemoryRange(file, file_offset, 0, bytes, flags, reloc_base, false);
}

static void SynchroniseVirt(struct virt_page* vp) {
    ArchSyncVirt(vp->vas, vp);
}

static void CleanupPageOrigin(void* _po) {
    struct page_origin* po = _po;
    if (po->file != NULL) {
        DerefObject(po->file);
    }
    DestroyStaticMutex(&po->mtx);
    FreeVmmHeap(po);
}

static void CleanupVas(void* _vas) {
    struct vas* vas = _vas;

    /*
     * TODO: actual cleanup.
     *
     * Whoever writes this: virt_pages hold a back-pointer to their vas with no
     * reference (deliberately - see vmm.h), so this cannot simply free the
     * tables. Every page has to be evicted from its phys_page (dropping the
     * phys_page's references to both the vas and the vp) BEFORE the mapping
     * tables go away, or SynchroniseVirt() will follow vp->vas into freed
     * memory. Also needs to free: both intermediate table levels, ->mappings,
     * ->lock, ->va, and the arch_data ArchInitVas() set up.
     */

    FreeHeap(vas);
}

static void CleanupVirtPage(void* _vp) {
    struct virt_page* vp = _vp;

    /*
     * Reaching here while still registered on a phys_page would leave a
     * dangling pp->vp / chain entry. Nothing should ever deref us to zero in
     * that state - the phys_page holds a reference precisely to stop it.
     */
    assert(vp->busy == 0);

    if (!vp->present) {
        DerefObject(vp->origin);
    }
    FreeVmmHeap(vp);
}

void InitVmm(void) {
    RegisterObjectType(OBJTYPE_PAGE_ORIGIN, CleanupPageOrigin);
    RegisterObjectType(OBJTYPE_VAS, CleanupVas);
    RegisterObjectType(OBJTYPE_PAGE_VIRT, CleanupVirtPage);
    InitSpinlock(&vmm_heap_lock);
    InitHeapEx(&vmm_special_heap, VmmSpecialHeapGetMemory);
}

/* Caller must hold pp->lock. */
void CallOnVirtualUsers(struct phys_page* pp, void(*func)(struct phys_page*, struct virt_page*)) {
    if (pp->vas != NULL) {
        LockVas(pp->vas);
        func(pp, pp->vp);
        UnlockVas(pp->vas);
    }
    struct vas_chain* chain = pp->chain;
    while (chain != NULL) {
        LogPrintf("Has a chain... 0x%X\n", chain);
        LockVas(chain->vas);
        func(pp, chain->vp);
        UnlockVas(chain->vas);
        chain = chain->next;
    }
}

/*
 * The accessed/dirty bits have to be harvested HERE, while the PTE still
 * exists. SynchroniseVirt() below is what tears the mapping down, so anything
 * reading them afterwards (as the old UpdateLRUAndDirtyOnVirt did) was
 * sampling bits that had already been destroyed - the LRU was reading noise.
 */
static void EnterCriticalVirt(struct phys_page* pp, struct virt_page* vp) {
    LogPrintf("EnterCriticalVirt\n");
    if (vp->busy++ == 0) {
        ArchReadVirtDirtyAndAccessed(vp);
        pp->dirty |= vp->dirty;
        if (vp->accessed) {
            pp->lru |= 0x8000;
        }
        vp->accessed = 0;
        LogPrintf("SynchroniseVirt\n");
        LogPrintf("vp->vas = 0x%X\n", vp->vas);
        LogPrintf("vp->virt = 0x%X\n", vp->virt);
        LogPrintf("vp->phys = 0x%X\n", vp->phys);
        LogPrintf("pp->phys = 0x%X\n", GetPhysAddr(pp));
        SynchroniseVirt(vp);
        LogPrintf("Done SynchroniseVirt\n");
    }
    assert(vp->busy != 0);      /* overflow */
}

static void LeaveCriticalVirt(struct phys_page* pp, struct virt_page* vp) {
    (void) pp;
    assert(vp->busy > 0);       /* underflow - used to wrap silently at 2 bits */
    if (--vp->busy == 0) {
        SynchroniseVirt(vp);
    }
}

/* Don't go calling these willy-nilly! */
/* These prevent anyone else accessing the physical page while held. */
export void EnterPhysPageCriticalSection(struct phys_page* pp) {
    AcquireSpinlock(&pp->lock);
    CallOnVirtualUsers(pp, EnterCriticalVirt);
}

export void LeavePhysPageCriticalSection(struct phys_page* pp) {
    CallOnVirtualUsers(pp, LeaveCriticalVirt);
    ReleaseSpinlock(&pp->lock);
}

static void DiscardVirt(struct phys_page* pp, struct virt_page* vp) {
    /* Clear 'present' first: it is what says which arm of the union is live. */
    vp->present = 0;
    vp->origin = pp->origin;
    RefObject(vp->origin);
    SynchroniseVirt(vp);
}

/*
 * Discards a physical page if possible. If so, it will return the phys_page
 * object, with the critical section already held. If not, it will return NULL.
 *
 * TODO: this is deliberately the dumb version - it unmaps every candidate page
 * in the system on every reclaim attempt, just to sample LRU. Wants replacing
 * with a resuming clock hand that stops at the first acceptable page.
 */
static struct phys_page* FindDiscardPage(void) {
    struct phys_page* best = NULL;
    uint16_t min_lru = 0xFFFF;

    /*
     * Pass 1: age and sample. Exactly one page is held at a time and it is
     * always released before moving on. The old version kept 'chosen' locked
     * for the remainder of the scan while acquiring later pages, which gave two
     * concurrent reclaimers a textbook ABBA deadlock.
     */
    for (struct phys_page* curr = GetFirstPhysPage(); curr != NULL; curr = GetNextPhysPage(curr)) {
        LogPrintf("Checking 0x%X for discard...\n", curr);
        /* Cheap pre-filter. State can change before we take the critical
         * section, but we're only ruling pages out, so it can't hurt. */
        AcquireSpinlock(&curr->lock);
        bool candidate = curr->wired == 0
                      && (curr->origin != NULL && curr->origin->file != NULL)
                      && (curr->vas != NULL || curr->chain != NULL);
        if (candidate) {
            curr->lru >>= 1;    /* age first; Enter puts the new bit back in */
        }
        ReleaseSpinlock(&curr->lock);

        if (!candidate) {
            continue;
        }

        LogPrintf("Is a candidate...\n");
        EnterPhysPageCriticalSection(curr);
        LogPrintf("Entered critical section...\n");
        bool usable = curr->wired == 0
                   && !curr->dirty
                   && curr->origin != NULL
                   && curr->origin->file != NULL;
        uint16_t lru = curr->lru;
        LeavePhysPageCriticalSection(curr);

        LogPrintf("With LRU = 0x%X\n", lru);

        if (usable && lru < min_lru) {
            min_lru = lru;
            best = curr;
        }
    }

    LogPrintf("Phys loop done.\n");

    if (best == NULL) {
        return NULL;
    }

    LogPrintf("Starting pass 2\n");

    /* Pass 2: re-acquire and revalidate; the world moved while we scanned. */
    EnterPhysPageCriticalSection(best);
    if (best->wired
        || best->dirty
        || best->origin == NULL
        || best->origin->file == NULL
        || (best->vas == NULL && best->chain == NULL)) {
        LeavePhysPageCriticalSection(best);
        return NULL;
    }
    return best;
}

/*
 * Caller must hold pp->lock and vas->lock.
 *
 * NOTE: the chain arm is currently unreachable. CreateVirtPage() always mints a
 * private page_origin, so origin -> phys is 1:1 and a frame never has more than
 * one virtual user. It only comes alive once there's an origin cache keyed on
 * (file, file_offset) - until then the vas_chain allocated per fault is pure
 * waste, but the code is kept live so it's correct the day sharing lands.
 */
static void RegisterVasAsPhysUser(struct phys_page* pp, struct vas* vas,
                                  struct virt_page* vp, struct vas_chain** link) {
    RefObject(vas);
    RefObject(vp);

    /*
     * This page didn't get marked busy when we walked the critical section, as
     * it wasn't in the chain until now. Increment this here so when we leave
     * the critical section (with us newly added) we don't underflow.
     */
    vp->busy++;

    if (pp->vas == NULL) {
        pp->vas = vas;
        pp->vp  = vp;
        return;                 /* 'link' is untouched; caller frees it */
    }

    (*link)->vas  = vas;
    (*link)->vp   = vp;
    (*link)->next = pp->chain;
    pp->chain = *link;
    *link = NULL;               /* consumed; caller must NOT free it */
}

/*
 * Establishes a fixed mapping (MMIO and friends). No phys_page involvement, no
 * demand loading, no discard. The origin is dead the moment this returns and is
 * allowed to hit refcount zero - nothing will ever need to re-derive the
 * physical address, because a fixed page is never taken away.
 */
static bool HandleFixedFault(struct vas* vas, size_t virt, struct page_origin* origin) {
    size_t fixed_phys = origin->phys;

    LockVas(vas);
    struct virt_page* vp = GetVirtualPageFromVirt(vas, virt);
    if (vp == NULL || vp->present || vp->origin != origin) {
        UnlockVas(vas);
        return false;           /* someone else got here first; retry */
    }

    DerefObject(vp->origin);    /* the vp's reference; ours still holds it up */
    vp->phys = fixed_phys;
    vp->present = 1;
    SynchroniseVirt(vp);
    UnlockVas(vas);
    return true;
}

void HandlePageFault(size_t virt, int fault_flags) {
    struct vas* vas = GetCurrentVas();
    int retries = 0;

retry:
    if (++retries > VMM_MAX_FAULT_RETRIES) {
        Panic(PANIC_VMM_LIVELOCK);
    }

    LockVas(vas);

    /*
     * For lazy loading of global page tables, etc.
     */
    if (ArchTryHandleSpecialPageFault(virt)) {
        UnlockVas(vas);
        return;
    }

    struct virt_page* vp = GetVirtualPageFromVirt(vas, virt);
    if (vp == NULL) {
        UnlockVas(vas);
        //DeliverSegvOrPanic(virt, fault_flags);
        return;
    }

    if (vp->present) {
        if (vp->busy > 0) {
            /* Temporarily unmapped by someone's critical section. Back off and
             * let the instruction re-fault once they're done. */
            UnlockVas(vas);
            //YieldCpu();
            return;
        }
        /*
         * Present and not busy. Either a stale TLB entry / another CPU already
         * serviced this (harmless, just return and re-execute), or it's a real
         * protection violation - a write to a read-only page, user touching a
         * kernel page, executing a no-execute page. The old code returned
         * unconditionally, which turned every permission fault into an infinite
         * fault loop.
         */
        bool violation = ((fault_flags & PF_WRITE) && !vp->write)
                      || ((fault_flags & PF_USER)  && !vp->user)
                      || ((fault_flags & PF_FETCH) && !vp->executable);
        UnlockVas(vas);
        if (violation) {
            //DeliverSegvOrPanic(virt, fault_flags);
        }
        return;
    }

    struct page_origin* origin = vp->origin;
    RefObject(origin);                      /* keep it alive across the unlock */
    UnlockVas(vas);

    if (origin->fixed) {
        bool ok = HandleFixedFault(vas, virt, origin);
        DerefObject(origin);
        if (!ok) {
            goto retry;
        }
        return;
    }

    size_t phys = origin->phys;             /* unlocked peek, rechecked below */

    if (phys == 0) {
        AcquireMutex(&origin->mtx, TIMEOUT_INFINITE);
        if (origin->phys == 0) {
            size_t frame = AllocPhys(true);
            if (frame == 0) {
                ReleaseMutex(&origin->mtx);
                DerefObject(origin);
                /* TODO: kill the offending process instead of the machine. */
                Panic(PANIC_OUT_OF_MEMORY);
            }

            /*
             * Fill the frame BEFORE publishing origin->phys. Nothing can reach
             * it until then, so this needs no frame lock and no other thread
             * can observe it half-initialised.
             *
             * The old code published first and zeroed later via a
             * 'phys_needs_setting' flag, which was broken two ways: a second
             * faulter saw a non-zero phys, so nobody zeroed the frame and the
             * previous owner's data leaked through; and a faulter that lost a
             * retry race carried the flag forward and memcpy'd zeroes over a
             * completely different frame that someone else had just filled.
             */
            if (origin->file != NULL) {
                /* TODO: read PAGE_SIZE at origin->file_offset into 'frame',
                 * zero-filling any tail past EOF, and apply origin->rebase_page
                 * relocations. */
                //ReadPageFromFile(origin, frame);
            } else {
                /* TODO: swap-in belongs here too, once there's a swapfile. */
                ZeroPhysPage(frame);
            }

            struct phys_page* newpp = GetPhysPage(frame);
            AcquireSpinlock(&newpp->lock);
            assert(newpp->origin == NULL && newpp->vas == NULL && newpp->chain == NULL);
            newpp->origin = origin;
            RefObject(origin);
            newpp->dirty = 0;
            newpp->wired = 0;
            newpp->lru = 0xFFFF;            /* fresh: don't reclaim immediately */
            ReleaseSpinlock(&newpp->lock);

            origin->phys = frame;           /* publish last */
        }
        ReleaseMutex(&origin->mtx);
        DerefObject(origin);
        goto retry;                         /* uniform re-entry through the dedup path */
    }

    /* Allocate while faulting is still permitted. */
    struct vas_chain* link = AllocVmmHeap(sizeof(struct vas_chain));
    if (link == NULL) {
        DerefObject(origin);
        Panic(PANIC_OUT_OF_MEMORY);
    }

    struct phys_page* pp = GetPhysPage(phys);
    EnterPhysPageCriticalSection(pp);       /* phys outer - matches discard's order */
    if (pp->origin != origin) {
        LeavePhysPageCriticalSection(pp);
        FreeVmmHeap(link);
        DerefObject(origin);
        goto retry;
    }

    LockVas(vas);                           /* re-acquired inner, to commit */
    vp = GetVirtualPageFromVirt(vas, virt); /* revalidate - vas was unlocked */
    if (vp == NULL || vp->present || vp->origin != origin) {
        UnlockVas(vas);
        LeavePhysPageCriticalSection(pp);
        FreeVmmHeap(link);
        DerefObject(origin);
        goto retry;
    }

    /* vp->phys and vp->origin are shared in a UNION! */
    DerefObject(vp->origin);                /* the vp's ref; ours keeps it alive */

    vp->phys = phys;
    vp->present = 1;
    RegisterVasAsPhysUser(pp, vas, vp, &link);  /* under both locks - discard can't miss it */
    UnlockVas(vas);

    /*
     * Leaving takes our new user's busy count 1 -> 0, which is what actually
     * installs the PTE. The old --busy/Sync/++busy dance here only existed so
     * the handler could memcpy through vp->virt; the frame is already populated
     * by the time we get here now.
     */
    LeavePhysPageCriticalSection(pp);

    DerefObject(origin);
    if (link != NULL) {
        FreeVmmHeap(link);
    }
}

export struct phys_page* DiscardPage(void) {
    struct phys_page* pp = FindDiscardPage();
    LogPrintf("Found a page to discard! 0x%X\n", pp);
    LogPrintf("PHYS = 0x%X\n", GetPhysAddr(pp));

    if (pp == NULL) {
        return NULL;
    }

    /*
     * Unmap first, THEN detach. While origin->phys still points here, a
     * concurrent faulter that sees present == 0 will come to GetPhysPage(),
     * block on our spinlock, and find pp->origin == NULL when it gets in - so
     * it retries cleanly. Clearing origin->phys up front (as before) opened a
     * window where a faulter could allocate a replacement frame while the old
     * one was still mapped.
     */
    CallOnVirtualUsers(pp, DiscardVirt);

    assert(pp->origin->phys == GetPhysAddr(pp));
    pp->origin->phys = 0;

    CallOnVirtualUsers(pp, LeaveCriticalVirt);  /* busy--/re-sync, list intact */

    if (pp->vas != NULL) {
        DerefObject(pp->vas);
        DerefObject(pp->vp);
    }
    pp->vas = NULL;
    pp->vp = NULL;

    struct vas_chain* curr = pp->chain;
    while (curr != NULL) {
        DerefObject(curr->vas);
        DerefObject(curr->vp);
        struct vas_chain* next = curr->next;
        FreeVmmHeap(curr);
        curr = next;
    }
    DerefObject(pp->origin);
    FreeDiscardedPhys(pp);    
    ReleaseSpinlock(&pp->lock);
    return pp;
}

export void CopyToPhysPage(size_t phys, void* data) {
    size_t r = ArchLockToCpu();
    size_t virt = ArchGetTemporaryPage(phys);
    memcpy((void*) virt, data, PAGE_SIZE);
    ArchReleaseTemporaryPage(virt);
    ArchUnlockFromCpu(r);
}

export void ZeroPhysPage(size_t phys) {
    size_t r = ArchLockToCpu();
    size_t virt = ArchGetTemporaryPage(phys);
    memset((void*) virt, 0, PAGE_SIZE);
    ArchReleaseTemporaryPage(virt);
    ArchUnlockFromCpu(r);
}
