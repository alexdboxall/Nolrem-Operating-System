#include <common.h>
#include <spinlock.h>
#include <arch.h>
#include <allocvirt.h>
#include <heap.h>

/* 
 * FreeVirtEx USES THE HEAP! (TO ALLOCATE!)
 * YOU CAN'T CALL FreeVirtEx IN THE 'REAL' VMM (THE ONE RESPONSIBLE FOR DOING
 * PAGE FAULT HANDLING!) IF YOU'RE ON THE 'FAULT PATH'!
 */
struct virt_range {
    size_t start_addr;
    size_t pages;
    struct virt_range* next;
};
 
export void InitVirtArena(struct virt_arena* va, size_t start, size_t bytes) {
    va->free_list_head = AllocHeap(sizeof(struct virt_range));
    va->free_list_head->start_addr = start;
    va->free_list_head->pages = (bytes + PAGE_SIZE - 1) / PAGE_SIZE;
    va->free_list_head->next = NULL;
    InitSpinlock(&va->lock);
}

export size_t AllocVirtEx(struct virt_arena* va, size_t bytes) {
    size_t pages = (bytes + PAGE_SIZE - 1) / PAGE_SIZE;

    AcquireSpinlock(&va->lock);

    struct virt_range* prev = NULL;
    struct virt_range* curr = va->free_list_head;

    while (curr) {
        if (curr->pages >= pages) {
            size_t retv = curr->start_addr;

            if (curr->pages == pages) {
                if (prev) {
                    prev->next = curr->next;
                } else {
                    va->free_list_head = curr->next;
                }
                ReleaseSpinlock(&va->lock);
                FreeHeap(curr);
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

export void FreeVirtEx(struct virt_arena* va, size_t start_addr, size_t bytes) {
    size_t pages = (bytes + PAGE_SIZE - 1) / PAGE_SIZE;
    size_t end_addr = start_addr + pages * PAGE_SIZE;

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
        FreeHeap(curr);
        return;
    }

    if (merged_left) {
        prev->pages += pages;
        ReleaseSpinlock(&va->lock);
        return;
    }

    if (merged_right) {
        curr->start_addr = start_addr;
        curr->pages += pages;
        ReleaseSpinlock(&va->lock);
        return;
    }

    struct virt_range* node = AllocHeap(sizeof(struct virt_range));
    node->start_addr = start_addr;
    node->pages = pages;
    node->next = curr;
    if (prev) {
        prev->next = node;
    } else {
        va->free_list_head = node;
    }
    ReleaseSpinlock(&va->lock);
}
