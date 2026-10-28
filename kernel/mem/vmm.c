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
#include <scheduler.h>
#include <vfs.h>
#include <aio.h>

/*
 * A fault should converge in a couple of trips at most. Anything beyond this
 * means a retry path exists that can never make progress, which used to hang
 * the machine in silence. Fail loudly instead.
 */
#define VMM_MAX_FAULT_RETRIES   100

static struct page_origin* CreatePageOrigin(struct file* file, size_t file_offset,
                                            const struct virt_loader* loader,
                                            size_t phys, bool fixed);

void LockVas(struct vas* vas) {
    AcquireMutex(vas->lock, TIMEOUT_INFINITE);
}

void UnlockVas(struct vas* vas) {
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
static uint8_t vmm_heap_bootstrap_page[1024];
static bool used_vmm_bootstrap_page = false;

void* VmmSpecialHeapGetMemory(size_t* bytes) {
    if (!used_vmm_bootstrap_page) {
        assert(*bytes <= sizeof(vmm_heap_bootstrap_page));
        used_vmm_bootstrap_page = true;
        *bytes = sizeof(vmm_heap_bootstrap_page);
        return vmm_heap_bootstrap_page;

    } else {
        size_t virt = AllocVirt(*bytes);
        size_t pages = (*bytes + PAGE_SIZE - 1) / PAGE_SIZE;
        for (size_t i = 0; i < pages; ++i) {
            ArchMapKernelPageDirectly(AllocPhys(true), virt + i * PAGE_SIZE);
        }
        *bytes = pages * PAGE_SIZE;
        return (void*) virt;
    }
}

struct spinlock vmm_heap_lock;
void* AllocVmmHeap(size_t bytes) {
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
    struct vas* vas = AllocPermanentHeap(sizeof(struct vas));
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

/*
 * 'loader' is copied into the origin (file pages only) and may be NULL, which
 * gives a zeroed loader, i.e. the default read and no unmap hook.
 */
static struct page_origin* CreatePageOrigin(struct file* file, size_t file_offset,
                                            const struct virt_loader* loader,
                                            size_t phys, bool fixed) {

    struct page_origin* po = AllocVmmHeap(
        file == NULL ? SIZEOF_PAGE_ORIGIN_ANON : SIZEOF_PAGE_ORIGIN_FILE
    );
    if (po == NULL) {
        return NULL;
    }
    InitStaticMutex(&po->mtx);
    InitObject(po, OBJTYPE_PAGE_ORIGIN);
    po->is_file = file != NULL;
    po->phys = phys;
    po->fixed = fixed;
    if (po->is_file) {
        po->file = file;
        po->file_offset = file_offset;
        if (loader != NULL) {
            po->loader = *loader;
        } else {
            memset(&po->loader, 0, sizeof(po->loader));
        }
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
    vp->fixed = false;
    vp->phys = existing_phys;

    struct phys_page* pp = GetPhysPage(existing_phys);
    pp->vp = vp;
    pp->vas = vas;
    pp->chain = NULL;

    LogPrintf("Page VIRT=0x%X, PHYS=0x%X is now discardable.\n", existing_virt, existing_phys);
    RefObject(vp);
    RefObject(vas);
    pp->origin = CreatePageOrigin(file, file_offset, NULL, existing_phys, false);
    LogPrintf("Origin set!\n");
    LockVas(vp->vas);
    struct virt_page** slot = GetVirtualPageSlot(vas, existing_virt, true);
    if (slot == NULL || *slot != NULL) {
        Panic(PANIC_FUCK_ME);
    }
    *slot = vp;
    UnlockVas(vp->vas);
    LogPrintf("Done.\n");
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

    struct obj_header* dummy_file = AllocHeap(sizeof(struct file));
    LogPrintf("The dummy file is at: 0x%X\n", dummy_file);
    InitObject(dummy_file, OBJTYPE_FILE);
    for (size_t i = k_start_page; i < k_end_page; ++i) {
        // TODO: need a real file! And offset! To know where to reload them from!
        MarkKernelPageDiscardable(i * PAGE_SIZE, i * PAGE_SIZE - ARCH_KRNL_MAPPING_BASE, false, (void*) dummy_file, 0xCAFEBABE);
    }
    for (size_t i = u_start_page; i < u_end_page; ++i) {
        // TODO: need a real file! And offset! To know where to reload them from!
        MarkKernelPageDiscardable(i * PAGE_SIZE, i * PAGE_SIZE - ARCH_KRNL_MAPPING_BASE, true, (void*) dummy_file, 0xCAFEBABE);
    }
}

/*
 * NOTE: 'fixed' is new. A fixed mapping carries a valid physical address from
 * birth, has no backing file, and must never be routed through the phys_page
 * dedup machinery. Previously AllocFixedMemory() produced an origin that the
 * fault handler could never satisfy, and it spun on 'goto retry' forever.
 */
static struct virt_page* CreateVirtPage(struct vas* vas, size_t virt, int flags,
                                        struct file* file, size_t file_offset,
                                        const struct virt_loader* loader,
                                        size_t phys, bool fixed) {

    if ((!!(flags & VP_WRITE)) && (!!(flags & VP_EXEC))) {
        return NULL;
    }
    struct virt_page* vp = AllocVmmHeap(sizeof(struct virt_page));
    if (vp == NULL) {
        return NULL;
    }

    struct page_origin* po = CreatePageOrigin(file, file_offset, loader, phys, fixed);
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
    vp->fixed = fixed;
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
                              size_t bytes, int flags,
                              const struct virt_loader* loader, bool fixed) {
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
        struct virt_page* vp = CreateVirtPage(
            vas,
            base + i * PAGE_SIZE,
            flags,
            file,
            file != NULL ? file_offset + i * PAGE_SIZE : 0,
            loader,
            phys != 0 ? phys + i * PAGE_SIZE : 0,
            fixed
        );
        if (vp == NULL) {
            UnwindVirtPages(vas, base, i);
            FreeVirt(base, pages * PAGE_SIZE);
            return NULL;
        }
    }

    return (void*) base;
}

export void* AllocFixedMemory(size_t phys, size_t bytes, int flags) {
    return AllocMemoryRange(NULL, 0, phys, bytes, flags, NULL, true);
}

export void* AllocAnonMemory(size_t bytes, int flags) {
    return AllocMemoryRange(NULL, 0, 0, bytes, flags, NULL, false);
}

export void* AllocFileMemory(struct file* file, size_t file_offset, size_t bytes,
                             int flags, const struct virt_loader* loader) {
    return AllocMemoryRange(file, file_offset, 0, bytes, flags, loader, false);
}

static void SynchroniseVirt(struct virt_page* vp) {
    ArchSyncVirt(vp->vas, vp);
}

static void CleanupPageOrigin(void* _po) {
    struct page_origin* po = _po;
    if (po->is_file) {
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
    InitHeapEx(&vmm_special_heap, VmmSpecialHeapGetMemory, NULL);
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
    if (vp->busy++ == 0) {
        ArchReadVirtDirtyAndAccessed(vp);
        pp->dirty |= vp->dirty;
        if (vp->accessed) {
            pp->lru |= 0x8000;
        }
        vp->accessed = 0;
        SynchroniseVirt(vp);
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
static void EnterPhysPageCriticalSection(struct phys_page* pp) {
    AcquireSpinlock(&pp->lock);
    CallOnVirtualUsers(pp, EnterCriticalVirt);
}

static void LeavePhysPageCriticalSection(struct phys_page* pp) {
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
 * Clock hand: resumes where the last reclaim stopped. Racy updates between
 * concurrent reclaimers are harmless - the worst case is both start at the
 * same page and the second blocks on pp->lock and then fails the recheck.
 */
static struct phys_page* clock_hand = NULL;

#define LRU_REFERENCED 0x8000

/*
 * Discards a physical page if possible. If so, it will return the phys_page
 * object, with the critical section already held. If not, it will return NULL.
 *
 * Second-chance clock: the accessed bit harvested by EnterCriticalVirt()
 * lands in LRU_REFERENCED. A referenced page has the bit cleared and is
 * skipped; an unreferenced one is the victim. Two full sweeps are enough to
 * either find a victim or conclude there isn't one.
 */
static struct phys_page* FindDiscardPage(void) {
    struct phys_page* curr = clock_hand != NULL ? clock_hand : GetFirstPhysPage();
    int wraps = 0;

    while (curr != NULL) {
        struct phys_page* pp = curr;

        curr = GetNextPhysPage(pp);
        if (curr == NULL) {
            curr = GetFirstPhysPage();
            if (++wraps > 2) {
                break;
            }
        }

        /* Cheap pre-filter under the lock, so we only unmap real candidates. */
        AcquireSpinlock(&pp->lock);
        bool candidate = pp->wired == 0
                      && pp->origin != NULL && pp->origin->is_file
                      && (pp->vas != NULL || pp->chain != NULL);
        ReleaseSpinlock(&pp->lock);
        if (!candidate) {
            continue;
        }

        EnterPhysPageCriticalSection(pp);   /* harvests accessed/dirty */
        bool usable = pp->wired == 0
                   && !pp->dirty
                   && pp->origin != NULL && pp->origin->is_file
                   && (pp->vas != NULL || pp->chain != NULL);

        if (usable && !(pp->lru & LRU_REFERENCED)) {
            clock_hand = curr;
            return pp;                      /* still held, as DiscardPage expects */
        }

        if (usable) {
            pp->lru &= ~LRU_REFERENCED;     /* second chance */
        }
        LeavePhysPageCriticalSection(pp);
    }

    clock_hand = curr;
    return NULL;
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

/*
 * Temporary kernel mapping of a physical frame, for loaders. Unlike
 * ArchGetTemporaryPage() this doesn't pin us to a CPU, so it is safe to block
 * (e.g. on file IO) while the mapping is held.
 */
void* VmmMapFrame(size_t frame) {
    size_t virt = AllocVirt(PAGE_SIZE);
    if (virt == 0) {
        return NULL;
    }
    ArchMapKernelPageDirectly(frame, virt);
    return (void*) virt;
}

void VmmUnmapFrame(void* virt) {
    ArchReleaseTemporaryPage((size_t) virt);
    FreeVirt((size_t) virt, PAGE_SIZE);
}

/*
 * The default reloader: zero the frame, then read one page from the file at
 * the origin's offset. Signature matches virt_loader::reload_func so custom
 * loaders can chain to it and then post-process the frame.
 */
int VmmDefaultReload(size_t frame, struct virt_loader* vr, struct page_origin* po) {
    (void) vr;

    void* virt = VmmMapFrame(frame);
    if (virt == NULL) {
        return ENOMEM;
    }

    /* Account for short reads (file size doesn't align to PAGE_SIZE). */
    memset(virt, 0, PAGE_SIZE);

    struct aio aio;
    struct transfer tr = CreateKernelTransfer(virt, PAGE_SIZE, po->file_offset, TRANSFER_READ);
    CreateIO(&aio, po->file, tr, NULL, NULL);
    int res = ReadFile(&aio);
    if (res == 0) {
        res = WaitIO(&aio, TIMEOUT_INFINITE);
    }
    VmmUnmapFrame(virt);

    return res;
}

/* Only call on file origins. May block; called under origin->mtx. */
static int LoadOriginIntoFrame(struct page_origin* origin, size_t frame) {
    if (origin->loader.reload_func != NULL) {
        return origin->loader.reload_func(frame, &origin->loader, origin);
    }
    return VmmDefaultReload(frame, &origin->loader, origin);
}

/*
 * The frame has been detached from 'origin' and is about to be freed, but its
 * contents are still intact. The is_file check is load-bearing: anon origins
 * are allocated without the trailing file fields, so touching ->loader on one
 * would read off the end of the allocation.
 */
static void UnloadOriginFromFrame(struct page_origin* origin, size_t frame) {
    if (origin->is_file && origin->loader.unmap_func != NULL) {
        origin->loader.unmap_func(frame, &origin->loader, origin);
    }
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
        /* TODO: depending on the cause/type, this could instead send SIGSEGV*/
        Panic(PANIC_PAGE_FAULT_IN_NON_PAGED_AREA);
        //DeliverSegvOrPanic(virt, fault_flags);
        return;
    }

    if (vp->present) {
        if (vp->busy > 0) {
            /* Temporarily unmapped by someone's critical section. Back off and
             * let the instruction re-fault once they're done. */
            UnlockVas(vas);
            Schedule();
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
            /* TODO: depending on the cause/type, this could instead send SIGSEGV*/
            Panic(PANIC_PAGE_FAULT_IN_NON_PAGED_AREA);
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
            size_t frame;
            if (origin->is_file) {
                frame = AllocPhys(true);    /* unpinned manually see later newpp->wired = 0*/
            } else {
                frame = AllocZeroedPhys();
            }
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
             * File frames come from AllocPhys(), which does not zero, so the
             * loader (default or custom) must initialise the entire page.
             */
            if (origin->is_file) {
                int res = LoadOriginIntoFrame(origin, frame);
                if (res != 0) {
                    /* free 'frame' here */
                    ReleaseMutex(&origin->mtx);
                    DerefObject(origin);
                    Panic(PANIC_PAGE_FAULT_IN_NON_PAGED_AREA);  /* TODO: SIGBUS / kill process */
                }
            }

            struct phys_page* newpp = GetPhysPage(frame);
            AcquireSpinlock(&newpp->lock);
            assert(newpp->origin == NULL && newpp->vas == NULL && newpp->chain == NULL);
            newpp->origin = origin;
            RefObject(origin);
            newpp->dirty = 0;
            newpp->wired = 0;               /* manually unwire */
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

struct phys_page* DiscardPage(void) {
    struct phys_page* pp = FindDiscardPage();
    if (pp == NULL) {
        return NULL;
    }
    LogPrintf("Found a page to discard! 0x%X\n", GetPhysAddr(pp));

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

    /* pp->lock (a spinlock) is held here, so an unmap_func must not block. */
    UnloadOriginFromFrame(pp->origin, GetPhysAddr(pp));

    DerefObject(pp->origin);
    FreeDiscardedPhys(pp);
    ReleaseSpinlock(&pp->lock);
    return pp;
}

void ZeroPhysPage(size_t phys) {
    size_t r = ArchLockToCpu();
    size_t virt = ArchGetTemporaryPage(phys);
    memset((void*) virt, 0, PAGE_SIZE);
    ArchReleaseTemporaryPage(virt);
    ArchUnlockFromCpu(r);
}

static struct vas_chain* UnregisterVasAsPhysUser(struct phys_page* pp, struct virt_page* vp) {
    if (pp->vp == vp) {
        struct vas_chain* node = pp->chain;
        if (node != NULL) {
            pp->chain = node->next;
            pp->vas = node->vas;
            pp->vp = node->vp;
        } else {
            pp->vas = NULL;
            pp->vp = NULL;
        }
        return node;
    }

    struct vas_chain** link = &pp->chain;
    while (*link != NULL && (*link)->vp != vp) {
        link = &(*link)->next;
    }
    assert(*link != NULL);      /* present + matching phys => must be registered */
    struct vas_chain* node = *link;
    *link = node->next;
    return node;
}

static void ReleaseVirtPage(struct vas* vas, size_t virt) {
    int retries = 0;

    for (;;) {
        if (++retries > VMM_MAX_FAULT_RETRIES) {
            Panic(PANIC_VMM_LIVELOCK);
        }

        LockVas(vas);
        struct virt_page** slot = GetVirtualPageSlot(vas, virt, false);
        struct virt_page* vp = (slot == NULL) ? NULL : *slot;

        /* Double free, or a ptr/size that doesn't match the allocation. */
        assert(vp != NULL);
        if (vp == NULL) {
            UnlockVas(vas);
            return;
        }

        if (!vp->present) {
            /*
             * vp->origin is the live union arm; CleanupVirtPage drops it.
             * busy may be nonzero if a DiscardPage() is mid-flight on this vp;
             * that's fine, the discarder's registration still holds its own
             * reference to the vp and only drops it after busy returns to 0.
             */
            *slot = NULL;
            UnlockVas(vas);
            DerefObject(vp);                /* the slot's reference */
            return;
        }

        if (vp->fixed) {
            /*
             * Fixed pages never touch a phys_page, so nobody else ever bumps
             * busy. Use it as a "treat as unmapped" marker to make the sync
             * tear the PTE down, then put it back to 0 for CleanupVirtPage's
             * assert. 'present' stays 1 on purpose: the fault handler already
             * dropped the origin reference, and CleanupVirtPage only derefs
             * vp->origin when !present (vp->origin aliases vp->phys here!).
             */
            assert(vp->busy == 0);
            vp->busy++;
            SynchroniseVirt(vp);
            vp->busy--;
            *slot = NULL;
            UnlockVas(vas);
            DerefObject(vp);                /* the slot's reference */
            return;
        }

        /*
         * Present, backed by a frame. We hold vas->lock but the lock order is
         * phys outer / vas inner, so drop it, take the phys_page, and
         * revalidate. The extra reference keeps the vp alive across the gap.
         */
        size_t phys = vp->phys;
        RefObject(vp);
        UnlockVas(vas);

        struct phys_page* pp = GetPhysPage(phys);
        EnterPhysPageCriticalSection(pp);
        LockVas(vas);

        slot = GetVirtualPageSlot(vas, virt, false);
        if (slot == NULL || *slot != vp || !vp->present || vp->phys != phys) {
            /* Discarded (or discarded and refaulted) while we were waiting. */
            UnlockVas(vas);
            LeavePhysPageCriticalSection(pp);
            DerefObject(vp);
            continue;
        }

        assert(pp->wired == 0);             /* freeing memory someone still has wired */
        assert(vp->busy == 1);              /* ours, from the Enter above */

        /*
         * Enter already harvested accessed/dirty into pp->dirty and unmapped
         * the PTE. Drop the vp from the tables and from the phys_page's user
         * list, and take back the busy count Enter gave it: Leave won't visit
         * it any more, and we deliberately do NOT sync - it must stay unmapped.
         */
        *slot = NULL;
        vp->busy--;
        struct vas_chain* node = UnregisterVasAsPhysUser(pp, vp);
        UnlockVas(vas);

        struct page_origin* dead_origin = NULL;
        if (pp->vas == NULL && pp->chain == NULL) {
            /*
             * Last user gone. Detach the frame from its origin while still
             * under pp->lock: a concurrent faulter peeking origin->phys will
             * either see 0, or see the old value, block on our lock, and then
             * fail its (pp->origin != origin) recheck. Either way it can't
             * map the frame.
             *
             * (Dirty file-backed contents are simply dropped: there is no
             * write-back path anywhere in the VMM yet. A loader's unmap_func
             * is the place to add one for its own pages.)
             */
            dead_origin = pp->origin;
            assert(dead_origin != NULL && dead_origin->phys == phys);
            dead_origin->phys = 0;
            pp->origin = NULL;
        }
        LeavePhysPageCriticalSection(pp);   /* re-syncs any remaining sharers */

        /* Everything below runs with no phys_page / vas lock held. */
        if (dead_origin != NULL) {
            /*
             * The frame is detached but not yet freed, so its contents are
             * still valid for the loader. A new fault on this origin may
             * already be loading a different frame concurrently.
             */
            UnloadOriginFromFrame(dead_origin, phys);
            FreeDiscardedPhys(GetPhysPage(phys));
            DerefObject(dead_origin);       /* the phys_page's reference */
        }
        if (node != NULL) {
            FreeVmmHeap(node);
        }
        DerefObject(vas);                   /* registration's vas reference */
        DerefObject(vp);                    /* registration's vp reference */
        DerefObject(vp);                    /* the slot's reference */
        DerefObject(vp);                    /* ours, taken above - frees the vp */
        return;
    }
}

/*
 * Wires one page: faults it in if needed, then bumps the wire count on its
 * frame under pp->lock. FindDiscardPage() rechecks 'wired' under that same
 * lock, so once the increment lands the frame can't be discarded.
 *
 * Fixed pages (MMIO etc.) are never discarded, so they only need faulting in.
 */
static int WireVirtPage(struct vas* vas, size_t virt) {
    int retries = 0;

    for (;;) {
        if (++retries > VMM_MAX_FAULT_RETRIES) {
            Panic(PANIC_VMM_LIVELOCK);
        }

        LockVas(vas);
        struct virt_page* vp = GetVirtualPageFromVirt(vas, virt);
        if (vp == NULL) {
            UnlockVas(vas);
            return EFAULT;
        }
        if (vp->present && vp->fixed) {
            UnlockVas(vas);
            return 0;
        }
        if (!vp->present || vp->busy > 0) {
            /* Not resident (load it) or mid-critical-section (this backs off
             * and yields). Either way, go round again afterwards. */
            UnlockVas(vas);
            HandlePageFault(virt, 0);
            continue;
        }

        /* Present: phys outer / vas inner, so drop the vas lock, take the
         * frame, and revalidate - same dance as ReleaseVirtPage(). */
        size_t phys = vp->phys;
        RefObject(vp);
        UnlockVas(vas);

        struct phys_page* pp = GetPhysPage(phys);
        AcquireSpinlock(&pp->lock);
        LockVas(vas);

        if (GetVirtualPageFromVirt(vas, virt) != vp || !vp->present || vp->phys != phys) {
            /* Discarded (or discarded and refaulted) while we were waiting. */
            UnlockVas(vas);
            ReleaseSpinlock(&pp->lock);
            DerefObject(vp);
            continue;
        }

        assert(pp->wired != 0xFFFF);        /* overflow */
        pp->wired++;
        UnlockVas(vas);
        ReleaseSpinlock(&pp->lock);
        DerefObject(vp);
        return 0;
    }
}

/* The page must currently be wired by the caller, so it can't have moved. */
static void UnwireVirtPage(struct vas* vas, size_t virt) {
    LockVas(vas);
    struct virt_page* vp = GetVirtualPageFromVirt(vas, virt);
    assert(vp != NULL && vp->present);      /* unwire without a matching wire */
    if (vp == NULL || !vp->present || vp->fixed) {
        UnlockVas(vas);
        return;
    }
    size_t phys = vp->phys;
    UnlockVas(vas);

    struct phys_page* pp = GetPhysPage(phys);
    AcquireSpinlock(&pp->lock);
    assert(pp->wired > 0);                  /* underflow */
    if (pp->wired > 0) {
        pp->wired--;
    }
    ReleaseSpinlock(&pp->lock);
}

export int WireVirt(void* ptr, size_t bytes) {
    if (ptr == NULL || bytes == 0) {
        return EINVAL;
    }

    struct vas* vas = GetCurrentVas();
    size_t first = ((size_t) ptr) / PAGE_SIZE;
    size_t last  = (((size_t) ptr) + bytes - 1) / PAGE_SIZE;

    for (size_t pg = first; pg <= last; ++pg) {
        int res = WireVirtPage(vas, pg * PAGE_SIZE);
        if (res != 0) {
            for (size_t done = first; done < pg; ++done) {
                UnwireVirtPage(vas, done * PAGE_SIZE);
            }
            return res;
        }
    }
    return 0;
}

export void UnwireVirt(void* ptr, size_t bytes) {
    if (ptr == NULL || bytes == 0) {
        return;
    }

    struct vas* vas = GetCurrentVas();
    size_t first = ((size_t) ptr) / PAGE_SIZE;
    size_t last  = (((size_t) ptr) + bytes - 1) / PAGE_SIZE;

    for (size_t pg = first; pg <= last; ++pg) {
        UnwireVirtPage(vas, pg * PAGE_SIZE);
    }
}

/*
 * Frees a range returned by AllocAnonMemory(), AllocFixedMemory() or
 * AllocFileMemory(). 'bytes' must be exactly what was passed to the Alloc.
 *
 * Physical frames owned by anon/file pages are returned to the allocator;
 * frames behind fixed mappings are left alone. Dirty file pages are dropped,
 * not written back.
 */
export void FreeMemory(void* ptr, size_t bytes) {
    if (ptr == NULL || bytes == 0) {
        return;
    }

    size_t base = (size_t) ptr;
    assert(base % PAGE_SIZE == 0);

    struct vas* vas = GetCurrentVas();
    size_t pages = (bytes + PAGE_SIZE - 1) / PAGE_SIZE;

    for (size_t i = 0; i < pages; ++i) {
        ReleaseVirtPage(vas, base + i * PAGE_SIZE);
    }

    FreeVirt(base, pages * PAGE_SIZE);
}
