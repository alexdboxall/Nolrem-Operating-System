
#include <obj.h>
#include <spinlock.h>
#include <common.h>
#include <mutex.h>
#include <arch.h>
#include <string.h>
#include <vmm.h>
#include <errno.h>
#include <phys.h>
#include <mutex.h>
#include <allocvirt.h>
#include <log.h>

struct page_origin* CreatePageOrigin(struct file* file, size_t file_offset, size_t base, size_t phys);

void LockVas(struct vas* vas) {
    AcquireMutex(vas->lock, TIMEOUT_INFINITE);
}

void UnlockVas(struct vas* vas) {
    ReleaseMutex(vas->lock);
}

struct virt_page* GetVirtualPageFromVirt(struct vas* vas, size_t virt) {
    size_t index = virt / PAGE_SIZE;
    size_t level_1 = index / MAPPINGS_PER_LEVEL;
    size_t level_2 = index % MAPPINGS_PER_LEVEL;
    struct virt_page** table = vas->mappings[level_1];
    if (table == NULL) {
        return NULL;
    }
    return table[level_2];
}

void AddVirtPageToVas(struct vas* vas, struct virt_page* vp) {
    // Assumes VAS lock held.
    size_t index = vp->virt / PAGE_SIZE;
    size_t level_1 = index / MAPPINGS_PER_LEVEL;
    size_t level_2 = index % MAPPINGS_PER_LEVEL;
    struct virt_page** table = vas->mappings[level_1];
    if (table == NULL) {
        table = AllocHeap(MAPPINGS_PER_LEVEL * sizeof(struct virt_page*));
        memset(table, 0, MAPPINGS_PER_LEVEL * sizeof(struct virt_page*));
        vas->mappings[level_1] = table;
    }
    table[level_2] = vp;
}

static struct vas* current_vas = NULL;
static struct vas* kernel_vas = NULL;

export struct vas* GetKernelVas(void) {
    return kernel_vas;
}

export struct vas* GetCurrentVas(void) {
    return current_vas;
}


bool virt_initialised = false;

bool IsVirtInitialised(void) {
    return virt_initialised;
}

export struct vas* CreateVas(void) {
    struct vas* vas = AllocHeap(sizeof(struct vas));
    InitObject(vas, OBJTYPE_VAS);

    vas->lock = CreateMutex();
    vas->va = AllocHeap(sizeof(struct virt_arena));
    InitVirtArena(vas->va, ARCH_USER_AREA_BASE, ARCH_USER_AREA_LIMIT);

    vas->mappings = AllocHeap(MAPPINGS_PER_LEVEL * sizeof(struct virt_page*));
    memset(vas->mappings, 0, MAPPINGS_PER_LEVEL * sizeof(struct virt_page*));

    ArchInitVas(vas, false);
    return vas;
}

void CreateInitialVas(void) {
    struct vas* vas = AllocHeap(sizeof(struct vas));
    InitObject(vas, OBJTYPE_VAS);

    vas->va = GetKernelVirtArena();
    vas->lock = CreateMutex();
    vas->mappings = AllocHeap(MAPPINGS_PER_LEVEL * sizeof(struct virt_page*));
    memset(vas->mappings, 0, MAPPINGS_PER_LEVEL * sizeof(struct virt_page*));

    kernel_vas = vas;
    current_vas = vas;
    ArchInitVas(vas, true);
    
    virt_initialised = true;
}

export struct virt_page* CreateVirtPage(struct vas* vas, size_t virt, int flags, struct file* file, size_t file_offset, size_t base, size_t phys) {
    struct virt_page* vp = AllocHeap(sizeof(struct virt_page));
    InitObject(vp, OBJTYPE_PAGE_VIRT);

    vp->vas = vas;
    vp->virt = virt;

    vp->executable = !!(flags & VP_EXEC);
    vp->write = !!(flags & VP_WRITE);
    vp->user = !!(flags & VP_USER);

    vp->accessed = 0;
    vp->busy = 0;
    vp->dirty = 0;
    vp->present = 0;
    vp->phys = phys;
    vp->origin = CreatePageOrigin(file, file_offset, base, phys);

    LockVas(vas);
    AddVirtPageToVas(vas, vp);
    UnlockVas(vas);

    return vp;
}

export void* AllocFixedMemory(size_t phys, size_t bytes, int flags) {
    struct virt_page* vp = CreateVirtPage(GetCurrentVas(), AllocVirt(bytes), flags, NULL, 0, 0, phys);
    if (vp == NULL) {
        return NULL;
    }
    return (void*) vp->virt;
}

export void* AllocAnonMemory(size_t bytes, int flags) {
    struct virt_page* vp = CreateVirtPage(GetCurrentVas(), AllocVirt(bytes), flags, NULL, 0, 0, 0);
    if (vp == NULL) {
        return NULL;
    }
    return (void*) vp->virt;
}

export void* AllocFileMemory(struct file* file, size_t file_offset, size_t bytes, int flags, size_t reloc_base) {
    struct virt_page* vp = CreateVirtPage(GetCurrentVas(), AllocVirt(bytes), flags, file, file_offset, reloc_base, 0);
    if (vp == NULL) {
        return NULL;
    }
    return (void*) vp->virt;
}

static void SynchroniseVirt(struct virt_page* vp) {
    ArchSyncVirt(vp->vas, vp);
}

struct page_origin* CreatePageOrigin(struct file* file, size_t file_offset, size_t base, size_t phys) {
    struct page_origin* po = AllocHeap(sizeof(struct page_origin));
    InitObject(po, OBJTYPE_PAGE_ORIGIN);
    po->rebase_page = base;
    po->file = file;
    po->phys = phys;
    po->file_offset = file_offset;
    po->mtx = CreateMutex();
    if (file != NULL) {
        RefObject(file);
    }
    return po;
}

static void CleanupPageOrigin(void* _po) {
    struct page_origin* po = _po;
    if (po->file) {
        DerefObject(po->file);
    }
    FreeHeap(po);
}

static void CleanupVas(void* _vas) {
    struct vas* vas = _vas;
    FreeHeap(vas);
}

static void CleanupVirtPage(void* _vp) {
    struct virt_page* vp = _vp;
    FreeHeap(vp);
}

void InitVmm(void) {
    RegisterObjectType(OBJTYPE_PAGE_ORIGIN, CleanupPageOrigin);
    RegisterObjectType(OBJTYPE_VAS, CleanupVas);
    RegisterObjectType(OBJTYPE_PAGE_VIRT, CleanupVirtPage);
}

void CallOnVirtualUsers(struct phys_page* pp, void(*func)(struct phys_page*, struct virt_page*)) {
    if (pp->vas != NULL) {
        LockVas(pp->vas);
        struct virt_page* vp = pp->vp;
        func(pp, vp);
        UnlockVas(pp->vas);
    }
    struct vas_chain* chain = pp->chain;
    while (chain) {
        LockVas(chain->vas);
        struct virt_page* vp = chain->vp;
        func(pp, vp);
        UnlockVas(chain->vas);
        chain = chain->next;
    }
}

static void EnterCriticalVirt(struct phys_page* pp, struct virt_page* vp) {
    int old = vp->busy++;
    if (old == 0) {
        SynchroniseVirt(vp);
    }
    (void) pp;
}

static void LeaveCriticalVirt(struct phys_page* pp, struct virt_page* vp) {
    int new = --vp->busy;
    if (new == 0) {
        SynchroniseVirt(vp);
    }
    (void) pp;
}

/* Don't go calling these willy-nilly! */
/* These prevent anyone else accessing the physical page while held. */
export void EnterPhysPageCriticalSection(struct phys_page* pp) {
    AcquireSpinlock(&pp->lock);
    pp->excl = 1;
    CallOnVirtualUsers(pp, EnterCriticalVirt);
}

export void LeavePhysPageCriticalSection(struct phys_page* pp) {
    CallOnVirtualUsers(pp, LeaveCriticalVirt);
    pp->excl = 0;
    ReleaseSpinlock(&pp->lock);
}

static void DiscardVirt(struct phys_page* pp, struct virt_page* vp) {
    vp->origin = pp->origin;
    RefObject(vp->origin);
    vp->present = 0;
    SynchroniseVirt(vp);
}

static void UpdateLRUAndDirtyOnVirt(struct phys_page* pp, struct virt_page* vp) {
    ArchReadVirtDirtyAndAccessed(vp);
    if (vp->accessed) {
        pp->lru |= 0x8000;
    }
    pp->dirty |= vp->dirty;
    vp->accessed = false;
}

/* Discards a physical page if possible. If so, it will return the phys_page
 * object, with the critical section already held. If not, it will return NULL.
 */
static struct phys_page* FindDiscardPage(void) {
    struct phys_page* curr = GetFirstPhysPage();
    struct phys_page* chosen = NULL;
    uint16_t min_lru = 0xFFFF;

    while (curr != NULL) {
        bool choosing_this_one = false; 
        /* Optimisation check. Sure, things can change from here to the critical
         * section acquire, but we're just ruling out pages, not doing anything
         * that affects correctness.*/
        AcquireSpinlock(&curr->lock);
        bool candidate = curr->wired == 0
                        && (curr->origin && curr->origin->file)
                        && (curr->vas || curr->chain);
        ReleaseSpinlock(&curr->lock);

        if (candidate) {
            EnterPhysPageCriticalSection(curr);
            curr->lru >>= 1;
            CallOnVirtualUsers(curr, UpdateLRUAndDirtyOnVirt);

            if (curr->wired == 0 && !curr->dirty && curr->origin && curr->origin->file) {
                uint16_t lru = curr->lru;
                if (lru < min_lru && !curr->dirty) {
                    min_lru = lru;
                    if (chosen != NULL) {
                        LeavePhysPageCriticalSection(chosen);
                    }
                    chosen = curr;
                    choosing_this_one = true;
                }
            }

            if (!choosing_this_one) {
                LeavePhysPageCriticalSection(curr);
            }
        }
        curr = GetNextPhysPage(curr);
    }

    return chosen;
}

static void RegisterVasAsPhysUser(
    struct phys_page* pp, 
    struct vas* vas, 
    struct virt_page* vp,
    struct vas_chain** link
) {
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
        *link = NULL;
        return;
    }

    (*link)->vas  = vas;
    (*link)->vp   = vp;
    (*link)->next = pp->chain;
    pp->chain = *link;
}


void HandlePageFault(size_t virt) {
    struct vas* vas = GetCurrentVas();

    bool phys_needs_setting = false;
retry:
    LockVas(vas);

    /* 
     * For lazy loading of global page tables, etc. 
     */
    bool handled = ArchTryHandleSpecialPageFault(virt);
    if (handled) {
        UnlockVas(vas);
        return;
    }

    struct virt_page* vp = GetVirtualPageFromVirt(vas, virt);
    struct page_origin* origin = vp->origin;
    UnlockVas(vas);                         // drop before crossing into phys/origin territory

    size_t phys = origin->phys;             // unlocked peek, rechecked below

    if (phys == 0) {
        AcquireMutex(origin->mtx, TIMEOUT_INFINITE);
        if (origin->phys == 0) {
            struct phys_page* newpp = GetPhysPage(AllocPhys(true));
            EnterPhysPageCriticalSection(newpp);   // safe: fresh frame, no discard can hold this yet
            newpp->origin = origin;
            RefObject(origin);
            origin->phys = GetPhysAddr(newpp);
            LeavePhysPageCriticalSection(newpp);
            phys_needs_setting = true;
        }
        ReleaseMutex(origin->mtx);
        goto retry;                          // uniform re-entry through the dedup path below
    }

    // Allocate while faulting is permitted.
    struct vas_chain* link = AllocHeap(sizeof(struct vas_chain));

    struct phys_page* pp = GetPhysPage(phys);
    EnterPhysPageCriticalSection(pp);        // phys outer — matches discard's own order
    bool matches = (pp->origin == origin);
    if (!matches) {
        LeavePhysPageCriticalSection(pp);
        goto retry;
    }

    LockVas(vas);                            // re-acquired inner, to commit
    vp = GetVirtualPageFromVirt(vas, virt);  // revalidate — vas was unlocked in between
    if (vp->origin != origin) {
        UnlockVas(vas);
        LeavePhysPageCriticalSection(pp);
        goto retry;
    }

    
    /* if phys_needs_setting, then:*/
    /* TODO: you probably load file data here into a buffer*/
    /* you probably memset the buffer to 0 otherwise .*/
    uint8_t buffer[PAGE_SIZE];
    if (phys_needs_setting) {
        if (vp->origin && vp->origin->file) {
            // TODO: load from disk
        } else {
            // TODO: this would also be where we load from swapfile

            // but for now it can only be inital load, so blank memory
            memset(buffer, 0, PAGE_SIZE);
        }
    }

    /* vp->phys and vp->origin are shared in a UNION! */
    DerefObject(vp->origin);

    vp->phys = phys;
    vp->present = 1;
    RegisterVasAsPhysUser(pp, vas, vp, &link);      // under both locks — discard can't miss this
    --vp->busy;
    SynchroniseVirt(vp);
    ++vp->busy;
    if (phys_needs_setting) {
        memcpy((void*) vp->virt, buffer, PAGE_SIZE);
    }
    UnlockVas(vas);

    LeavePhysPageCriticalSection(pp);

    if (link != NULL) {
        FreeHeap(link);
    }
}

export struct phys_page* DiscardPage(void) {
    struct phys_page* pp = FindDiscardPage();
    if (pp == NULL) {
        return NULL;
    }

    pp->origin->phys = 0;
    CallOnVirtualUsers(pp, DiscardVirt);
    CallOnVirtualUsers(pp, LeaveCriticalVirt);   // busy--/re-sync, list still intact

    if (pp->vas != NULL) {
        DerefObject(pp->vas);
        DerefObject(pp->vp);
    }
    pp->vas = NULL;
    pp->vp = NULL;
    struct vas_chain* curr = pp->chain;
    while (curr) {
        DerefObject(curr->vas);
        DerefObject(curr->vp);
        struct vas_chain* next = curr->next;
        FreeHeap(curr);
        curr = next;
    }
    pp->chain = NULL;
    DerefObject(pp->origin);
    pp->origin = NULL;
    pp->allocated = 0;
    pp->excl = 0;
    ReleaseSpinlock(&pp->lock);
    return pp;
}