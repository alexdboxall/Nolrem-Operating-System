#include <common.h>
#include <spinlock.h>
#include <arch.h>
#include <allocvirt.h>
#include <heap.h>
#include <vmm.h>
#include <panic.h>

/* 
 * FreeVirtEx USES THE HEAP! (TO ALLOCATE!)
 * YOU CAN'T CALL FreeVirtEx IN THE 'REAL' VMM (THE ONE RESPONSIBLE FOR DOING
 * PAGE FAULT HANDLING!) IF YOU'RE ON THE 'FAULT PATH'!
 * 
 * Note that if you're in the 'file backed page fault' path, you're not on the
 * critical path - it's the AllocVmmHeap chain (anon pages) that can't use this.
 */
struct virt_range {
    size_t start_addr;
    size_t pages;
    struct virt_range* next;
};
 
export void InitVirtArena(struct virt_arena* va, size_t start, size_t bytes) {
    va->free_list_head = AllocVmmHeap(sizeof(struct virt_range));
    if (va->free_list_head == NULL) {
        KePanic(PANIC_OUT_OF_MEMORY);       /* boot time: an arena with nothing in it is useless */
    }
    va->free_list_head->start_addr = start;
    va->free_list_head->pages = (bytes + PAGE_SIZE - 1) / PAGE_SIZE;
    va->free_list_head->next = NULL;
    InitSpinlock(&va->lock);
}

/*
 * `may_free_node` says whether we may give an arena node back to the VMM heap
 * (FreeVmmHeap, which takes vmm_heap_lock). The VMM heap's own grow callback has
 * to say no: it runs with vmm_heap_lock already held (it is called from inside
 * AllocVmmHeap), so freeing a node there would take that lock a second time.
 *
 * Without freeing, a range that is used up exactly is left in the list as an
 * empty one (zero pages, starting just past what was handed out). That is a
 * valid list entry: nothing can be allocated from it, and when the range that
 * was handed out is freed again, FreeVirtEx merges it straight back into it.
 */
static size_t AllocVirtExImpl(struct virt_arena* va, size_t bytes, bool may_free_node) {
    size_t pages = (bytes + PAGE_SIZE - 1) / PAGE_SIZE;

    AcquireSpinlock(&va->lock);

    struct virt_range* prev = NULL;
    struct virt_range* curr = va->free_list_head;

    while (curr) {
        if (curr->pages >= pages) {
            size_t retv = curr->start_addr;

            if (curr->pages == pages && may_free_node) {
                if (prev) {
                    prev->next = curr->next;
                } else {
                    va->free_list_head = curr->next;
                }
                ReleaseSpinlock(&va->lock);
                FreeVmmHeap(curr);
                return retv;

            } else {
                curr->start_addr += pages * PAGE_SIZE;
                curr->pages -= pages;
                ReleaseSpinlock(&va->lock);
                return retv;
            }
        }
        prev = curr;
        curr = curr->next;
    }

    ReleaseSpinlock(&va->lock);
    return 0; // no range large enough
}

export size_t AllocVirtEx(struct virt_arena* va, size_t bytes) {
    return AllocVirtExImpl(va, bytes, true);
}

/* For the VMM heap's grow callback only: never touches the VMM heap, see above. */
export size_t AllocVirtExNoHeap(struct virt_arena* va, size_t bytes) {
    return AllocVirtExImpl(va, bytes, false);
}

export void FreeVirtEx(struct virt_arena* va, size_t start_addr, size_t bytes) {
    size_t pages = (bytes + PAGE_SIZE - 1) / PAGE_SIZE;
    size_t end_addr = start_addr + pages * PAGE_SIZE;

    /*
     * A node for the range, in case it can't be merged into a neighbour. This has to be got before taking the
     * arena lock, not while holding it: if the VMM heap is full, growing it allocates from this arena
     * (VmmSpecialHeapGetMemory), which takes the arena lock, and we would be waiting on ourselves.
     */
    struct virt_range* spare = AllocVmmHeap(sizeof(struct virt_range));

    AcquireSpinlock(&va->lock);

    struct virt_range* prev = NULL;
    struct virt_range* curr = va->free_list_head;
    while (curr && curr->start_addr < start_addr) {
        prev = curr;
        curr = curr->next;
    }

    int merged_left = (prev && prev->start_addr + prev->pages * PAGE_SIZE == start_addr);
    int merged_right = (curr && end_addr == curr->start_addr);

    if (merged_left && merged_right) {
        prev->pages += pages + curr->pages;
        prev->next = curr->next;
        ReleaseSpinlock(&va->lock);
        FreeVmmHeap(curr);
        if (spare != NULL) FreeVmmHeap(spare);
        return;
    }

    if (merged_left) {
        prev->pages += pages;
        ReleaseSpinlock(&va->lock);
        if (spare != NULL) FreeVmmHeap(spare);
        return;
    }

    if (merged_right) {
        curr->start_addr = start_addr;
        curr->pages += pages;
        ReleaseSpinlock(&va->lock);
        if (spare != NULL) FreeVmmHeap(spare);
        return;
    }

    /* This is only the case if AllocVmmHeap returns NULL, i.e. the system is
     * out of memory.
     */
    if (spare == NULL) {
        /* Nowhere to record the range, so it is lost. That wastes address space but nothing else. */
        ReleaseSpinlock(&va->lock);
        return;
    }
    spare->start_addr = start_addr;
    spare->pages = pages;
    spare->next = curr;
    if (prev) {
        prev->next = spare;
    } else {
        va->free_list_head = spare;
    }
    ReleaseSpinlock(&va->lock);
}
