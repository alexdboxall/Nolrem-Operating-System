#include <common.h>
#include <panic.h>
#include <log.h>
#include <string.h>
#include <spinlock.h>
#include <heapex.h>
#include <string.h>

/* THIS FILE IS BYO LOCKING */

/**
 * Represents a section of memory that is either allocated or free. The memory 
 * address it represents is itself, excluding the metadata at the start or end.
 */
struct block {
    /*
     * The entire size of the block. The low 2 bits do not form part of the 
     * size, the low bit is set for allocated blocks, and the second lowest bit 
     * is indicates if it is a fencepost.
     */
    size_t size;

    /*
     * Only here on free blocks. Allocated blocks use this as the start of 
     * allocated memory.
     */
    struct block* next;
    struct block* prev;

    /*
     * At position size - sizeof(size_t), there is the trailing size tag. There
     * are no flags in the low bit of this value, unlike the heading size tag.
     */
};

/**
 * Must be a power of 2.
 */
#define ALIGNMENT 8

/**
 * The amount of metadata at the start and end of allocated blocks. The next and
 * free pointers in free blocks do not count.
 */
#define METADATA_LEADING (sizeof(size_t))
#define METADATA_TRIALING (sizeof(size_t))
#define METADATA_TOTAL (METADATA_LEADING + METADATA_TRIALING)

#define MIN_REQ_SIZE (2 * sizeof(size_t))

/*
 * heap->_free_list_bitmap (a uint32_t, which must be added to struct heap and
 * zeroed in InitHeapEx) has bit N set if and only if free list N is non-empty.
 * This means each free list needs a bit, and the layout of the lists below is
 * baked into GetInsertionIndex, so both are checked at compile time.
 */
#define LIST_BIT(index) (((uint32_t) 1) << (index))

_Static_assert(TOTAL_NUM_FREE_LISTS == 24, 
    "GetInsertionIndex assumes the 24 list layout in free_list_block_sizes");
_Static_assert(TOTAL_NUM_FREE_LISTS <= 32, 
    "The free list bitmap is a uint32_t, so there can be at most 32 lists");

/**
 * Index of the least significant set bit. The value MUST be non-zero, as the
 * result of BSF on a zero input is undefined.
 *
 * BSF/BSR have existed since the 386, so are fine on an i486 (unlike TZCNT,
 * LZCNT and POPCNT, which must not be used here).
 */
static userexec inline int BitScanForward(uint32_t value) {
#if defined(__i386__) || defined(__x86_64__)
    int result;
    __asm__ ("bsfl %1, %0" : "=r" (result) : "rm" (value) : "cc");
    return result;
#else
    return __builtin_ctz(value);
#endif
}

/**
 * Index of the most significant set bit (i.e. floor(log2(value))). The value 
 * MUST be non-zero, as the result of BSR on a zero input is undefined.
 */
static userexec inline int BitScanReverse(uint32_t value) {
#if defined(__i386__) || defined(__x86_64__)
    int result;
    __asm__ ("bsrl %1, %0" : "=r" (result) : "rm" (value) : "cc");
    return result;
#else
    return 31 - __builtin_clz(value);
#endif
}

/**
 * An array which holds the minimum allocation sizes that each free list can 
 * hold.
 *
 * This is no longer used to look up lists (GetInsertionIndex works it out with 
 * BSR), but it is the source of truth for what the layout is, and FindBlock 
 * uses it to pick the size to request from the system.
 */

 /*                                                 
 * Check that common sizes are exact matches:       ROUNDED UP TO ALIGNMENT
 sizeof(struct mutex      ):    0x00000018  = 24    24
 sizeof(struct sem        ):    0x00000018  = 24    24
 sizeof(struct aio        ):    0x0000005C  = 92    96
 sizeof(struct page_origin):    0x00000034  = 52    56
 sizeof(struct vas_chain  ):    0x0000000C  = 12    16
 sizeof(struct virt_page  ):    0x00000018  = 24    24
 sizeof(struct virt_arena ):    0x00000008  =  8    8
 sizeof(struct virt_range ):    0x0000000C  = 12    12
*/
static const userrodata uint16_t free_list_block_sizes[TOTAL_NUM_FREE_LISTS] = {
    8,          16,         24,         32,
    40,         48,         56,         64,
    80,         96,         112,        128,
    160,        192,        224,        256,
    320,        384,        448,        512,
    640,        768,        896,        1024
};

/**
 * Calculates which free list a block should be inserted in. This rounds down,
 * and so it should not normally be used to look up where a block should be.
 *
 * The table above has a very regular shape, which lets us skip searching it:
 *  - Below 64 bytes, the lists are in steps of 8 (index 0 is 8 bytes, index 6 
 *    is 56 bytes).
 *  - From 64 bytes upwards, every power of two is split into 4 lists. If the
 *    highest set bit of the size is k, the next two bits below it pick the 
 *    sub-list, and each power of two starts 4 lists after the previous one 
 *    (64 starts at index 7, 128 at 11, 256 at 15, 512 at 19).
 *  - 1024 and above all go in the last list.
 */
static userexec int GetInsertionIndex(size_t size_without_metadata) {
    if (size_without_metadata < 64) {
        if (size_without_metadata < 8) {
            return 0;
        }
        return (int) (size_without_metadata >> 3) - 1;
    }

    if (size_without_metadata >= 1024) {
        return TOTAL_NUM_FREE_LISTS - 1;
    }

    int k = BitScanReverse((uint32_t) size_without_metadata);
    int sublist = (int) ((size_without_metadata >> (k - 2)) & 3);
    return 7 + (k - 6) * 4 + sublist;
}

/**
 * Used to work out which free list a block should be in, when we are *reading*
 * a block. This rounds the size *up*, meaning it cannot be used to insert a 
 * block into a list. NOT USED TO INSERT BLOCKS!! 
 *
 * The smallest list that fits a size is one past the largest list whose 
 * minimum size is strictly less than it, i.e. one past the insertion index of 
 * (size - 1).
 */
static userexec int GetSmallestListIndexThatFits(size_t size_without_metadata) {
    if (size_without_metadata <= free_list_block_sizes[0]) {
        return 0;
    }

    int result = GetInsertionIndex(size_without_metadata - 1) + 1;
    if (result >= TOTAL_NUM_FREE_LISTS) {
        return TOTAL_NUM_FREE_LISTS - 1;
    }
    return result;
}

/**
 * Rounds up a user-supplied allocation size to the alignment. If it's less than
 * the minimum size internally supported, we will round it up to that size.
 */
static userexec size_t RoundUpSize(size_t size) {
    if (size < MIN_REQ_SIZE) {
        size = MIN_REQ_SIZE;
    }
    return (size + ALIGNMENT - 1) & ~(ALIGNMENT - 1);
}

static userexec void MarkFencepost(struct block* block) {
    block->size |= 2;
}

static userexec void MarkFree(struct block* block) {
    block->size &= ~1;
}

static userexec void MarkAllocated(struct block* block) {
    block->size |= 1;
}

static userexec bool IsAllocated(struct block* block) {
    return block->size & 1;
}

static userexec bool IsFencepost(struct block* block) {
    return block->size & 2;
}

/**
 * Given a block, returns its total size, including metadata. This takes into 
 * account the flags on the size field and removes them from the return value. 
 */
static userexec size_t GetSize(struct block* block) {
    return block->size & ~7;
}

static userexec bool DoesFreeBlockSpanEntireChunk(struct block* block) {
    if (IsAllocated(block)) {
        return false;
    }
    size_t size = GetSize(block);
    size_t prev_size = *(((size_t*) block) - 1);
    struct block* prev = (struct block*) (((size_t*) block) - prev_size / sizeof(size_t));
    struct block* next = (struct block*) (((size_t*) block) + size / sizeof(size_t));
    return IsFencepost(prev) && IsFencepost(next);
}

static userexec struct block** GetHeap(struct heap* heap) {
    return heap->_head_block;
}


/**
 * Sets the *total* size of a given block. This does not do any checking, so the
 * caller must be careful as setting the wrong size can corrupt the heap.
 * Carries the swappability and allocation tags with it from the front to the 
 * back tags.
 */
static userexec void SetSizeTags(struct block* block, size_t size) {
    block->size = (block->size & 7) | size;
    *(((size_t*) block) + (size / sizeof(size_t)) - 1) = size;
}

/**
 * Allocates a new block from the system that is able to hold the amount of data
 * specified. Also allocated enough memory for fenceposts on either side of the 
 * data, and sets up these fenceposts correctly.
 */
static userexec struct block* RequestBlock(struct heap* heap, size_t total_size) {
    /*
     * We need to add the extra bytes for fenceposts to be added. We must do 
     * this before we round up to the nearest areana size (if we did it after, 
     * it wouldn't be aligned anymore).
     */
    total_size += MIN_REQ_SIZE * 2;
    struct block* block = (struct block*) heap->get_memory(&total_size);
    if (block == NULL) {
        return NULL;
    }

    /*
     * Set the metadata for both the fenceposts and the main data block. 
     * Keep in mind that total_size now includes the fencepost metadata (see top
     * of function), so this sometimes needs to be subtracted off.
     */
    size_t actual_block_offset = MIN_REQ_SIZE / sizeof(size_t);
    size_t right_block_offset = (total_size - MIN_REQ_SIZE) / sizeof(size_t);

    struct block* left_fence = block;
    struct block* actual_block = (struct block*) (((size_t*) block) + actual_block_offset);
    struct block* right_fence  = (struct block*) (((size_t*) block) + right_block_offset);

    SetSizeTags(left_fence, MIN_REQ_SIZE);
    SetSizeTags(actual_block, total_size - 2 * MIN_REQ_SIZE);
    SetSizeTags(right_fence, MIN_REQ_SIZE);

    actual_block->prev = NULL;
    actual_block->next = NULL;
    MarkFencepost(left_fence);
    MarkFencepost(right_fence);
    MarkAllocated(left_fence);
    MarkAllocated(right_fence);
    MarkFree(actual_block);

    return actual_block;
}

/**
 * Removes a block from a free list. It needs to take in the exact free list's 
 * index (as opposed to calculating it itself), as this may be used halfway 
 * though allocations or deallocations where the block isn't yet in its correct 
 * block.
 *
 * This is one of the two places that changes which lists are non-empty (the 
 * other is AddBlock), so it must keep the bitmap up to date.
 */
static userexec void RemoveBlock(struct heap* heap, int free_list_index, struct block* block) {
    struct block** head_list = GetHeap(heap);

    if (block->prev == NULL && block->next == NULL) {
        head_list[free_list_index] = NULL;
        heap->_free_list_bitmap &= ~LIST_BIT(free_list_index);
        
    } else if (block->prev == NULL) {
        head_list[free_list_index] = block->next;
        block->next->prev = NULL;

    } else if (block->next == NULL) {
        block->prev->next = NULL;

    } else {
        block->prev->next = block->next;
        block->next->prev = block->prev;
    }
}

/**
 * Adds a block to its appropriate free list. It also coalesces the block with 
 * surrounding free blocks if possible.
 */
static userexec struct block* AddBlock(struct heap* heap, struct block* block) {
    size_t size = GetSize(block);
    struct block** head_list = GetHeap(heap);

    int free_list_index = GetInsertionIndex(size - METADATA_TOTAL);

    size_t prev_size = *(((size_t*) block) - 1);
    struct block* prev = (struct block*) (((size_t*) block) - prev_size / sizeof(size_t));
    struct block* next = (struct block*) (((size_t*) block) + size / sizeof(size_t));

    if (IsAllocated(prev) && IsAllocated(next)) {
        /*
         * Cannot coalesce here, so just add to the free list.
         */
        block->prev = NULL;
        block->next = head_list[free_list_index];
        if (block->next != NULL) {
            block->next->prev = block;
        }
        head_list[free_list_index] = block;
        heap->_free_list_bitmap |= LIST_BIT(free_list_index);
        MarkFree(block);
        return block;

    } else if (IsAllocated(prev) && !IsAllocated(next)) {
        /*
         * Need to coalesce with the one on the right.
         */
        RemoveBlock(heap, GetInsertionIndex(GetSize(next) - METADATA_TOTAL), next);
        SetSizeTags(block, size + GetSize(next));
        block->prev = NULL;
        block->next = NULL;
        MarkFree(block);    
        return AddBlock(heap, block);
    
    } else if (!IsAllocated(prev) && IsAllocated(next)) {
        /*
         * Need to coalesce with the one on the left.
         */
        RemoveBlock(heap, GetInsertionIndex(GetSize(prev) - METADATA_TOTAL), prev);
        SetSizeTags(prev, size + GetSize(prev));
        prev->prev = NULL;
        prev->next = NULL;
        MarkFree(prev);
        return AddBlock(heap, prev);

    } else {
        /*
         * Coalesce with blocks on both sides.
         */
        RemoveBlock(heap, GetInsertionIndex(GetSize(prev) - METADATA_TOTAL), prev);
        RemoveBlock(heap, GetInsertionIndex(GetSize(next) - METADATA_TOTAL), next);
        SetSizeTags(prev, size + GetSize(prev) + GetSize(next));
        prev->prev = NULL;
        prev->next = NULL;
        MarkFree(prev);
        return AddBlock(heap, prev);
    }
}

/*
 * Allocates a block. The block to be allocated will be the first block in the 
 * given free list, and that free list must be non-empty, and be able to fit the
 * requested size.
 */
static userexec struct block* AllocateBlock(
    struct heap* heap,
    struct block* block, 
    int free_list_index, 
    size_t user_requested_size
) {
    size_t total_size = user_requested_size + METADATA_TOTAL;
    size_t block_size = GetSize(block);

    if (block_size - total_size < MIN_REQ_SIZE + METADATA_TOTAL) {
        /*
         * We can just remove from the list altogether if the sizes match up 
         * exactly, or if there would be so little left over that we can't form 
         * a new block.
         */
        RemoveBlock(heap, free_list_index, block);
        /*
         * Prevent memory leak (from having a hole in memory), but do it after 
         * removing the block, as this may change the list it needs to be in, 
         * and RemoveBlock will not like that.
         */
        SetSizeTags(block, block_size);
        MarkAllocated(block);
        return block;

    } else {
        /*
         * We must split the block into two. If no list change is needed, we can
         * leave the 'leftover' parts in the list as is (just fixing up the size
         * tags), and then return the new block.
         */

        RemoveBlock(heap, free_list_index, block);

        size_t leftover = block_size - total_size;
        SetSizeTags(block, leftover);

        size_t offset = leftover / sizeof(size_t);
        struct block* allocated_block = (struct block*) (((size_t*) block) + offset);
        SetSizeTags(allocated_block, total_size);

        /*
         * Must be done before we try to move around the leftovers (or else it 
         * will coalesce back into one block). 
         */
        MarkAllocated(allocated_block);

        /*
        * We need to remove the leftover block from this list, and add it to the
        * correct list.
        */
        MarkFree(block);
        AddBlock(heap, block);
        return allocated_block;
    }
}

export userexec void PurgeFreeAreasFromHeapEx(struct heap* heap) {
    if (heap->return_memory == NULL) return;
    
    struct block** head_list = GetHeap(heap);

    /* Only look at the largest list class. */
restart:
    struct block* block = head_list[TOTAL_NUM_FREE_LISTS - 1];
    while (block) {
        if (DoesFreeBlockSpanEntireChunk(block)) {
            size_t size = GetSize(block);
            size_t prev_size = *(((size_t*) block) - 1);
            struct block* left_fencepost = (struct block*) (((size_t*) block) - prev_size / sizeof(size_t));
            struct block* right_fencepost = (struct block*) (((size_t*) block) + size / sizeof(size_t));

            /* Remove from free list */
            AllocateBlock(heap, block, TOTAL_NUM_FREE_LISTS - 1, GetSize(block) - METADATA_TOTAL);

            size_t start_addr = (size_t) left_fencepost;
            size_t length = ((size_t) right_fencepost) + MIN_REQ_SIZE - start_addr;
            heap->return_memory((void*) start_addr, length);
            goto restart;
            
        } else {
            block = block->next;
        }
    }
}

/**
 * Allocates a block that can fit the user requested size. It will request new 
 * memory from the system if required. If it returns NULL, then there is not 
 * enough memory of the system to satisfy the request.
 */
static userexec struct block* FindBlock(struct heap* heap, size_t user_requested_size) {
    struct block** head_list = GetHeap(heap);

    /*
     * Every list from min_index upwards holds blocks that are big enough (the
     * only exception being the last list, which holds a range of sizes and 
     * needs to be searched). So, mask off the lists that are too small, and 
     * the lowest remaining set bit is the smallest non-empty list that fits.
     */
    int min_index = GetSmallestListIndexThatFits(user_requested_size);
    uint32_t candidates = heap->_free_list_bitmap & ((~(uint32_t) 0) << min_index);

    if (candidates != 0) {
        int i = BitScanForward(candidates);
        if (i != TOTAL_NUM_FREE_LISTS - 1) {
            return AllocateBlock(heap, head_list[i], i, user_requested_size);
        }

        /*
         * The last list is the highest one, so if nothing in it fits then 
         * there is nowhere left to look.
         */
        struct block* current = head_list[i];
        while (current) {
            if (GetSize(current) - METADATA_TOTAL >= user_requested_size) {
                return AllocateBlock(heap, current, i, user_requested_size);
            }
            current = current->next;
        }
    }

    /*
     * If we can't find a block that will fit, then we must allocate memory.
     * Round up to the next block size, so we ensure we are in the next bucket.
     * This avoids an issue if e.g. a user requests 2.1KB, and we allocate 3.9KB
     * and it goes in the wrong bucket due to the two different indexes used.
     */
    size_t total_size;
    if (min_index + 1 < TOTAL_NUM_FREE_LISTS) {
        total_size = free_list_block_sizes[min_index + 1] + METADATA_TOTAL;
    } else {
        total_size = user_requested_size + METADATA_TOTAL;
    }
    struct block* sys_block = RequestBlock(heap, total_size);
    if (sys_block == NULL) {
        return NULL;
    }

    
    /*  
     * Put the new memory in the free list (which ought to be empty, as wouldn't
     * need to request new memory otherwise). Then we can allocate the block.
     */
    struct block* inserted = AddBlock(heap, sys_block);
    int inserted_index = GetInsertionIndex(GetSize(inserted) - METADATA_TOTAL);
    return AllocateBlock(heap, inserted, inserted_index, user_requested_size);
}

export userexec void* AllocHeapEx(struct heap* heap, size_t size) {
    if (size == 0) {
        return NULL;
    }

    size = RoundUpSize(size);
    struct block* block = FindBlock(heap, size);
    
    if (block == NULL) {
        return NULL;
    }
    heap->outstanding_allocated_memory += GetSize(block);
    return AddVoidPtr(block, METADATA_LEADING);
}

export userexec void FreeHeapEx(struct heap* heap, void* ptr) {
    if (ptr == NULL) {
        return;
    }

    struct block* block = SubVoidPtr(ptr, METADATA_LEADING);
    block->prev = NULL;
    block->next = NULL;

    heap->outstanding_allocated_memory -= GetSize(block);

    AddBlock(heap, block);
}

export userexec void* ReallocHeapEx(struct heap* heap, void* ptr, size_t new_size) {
    if (ptr == NULL) {
        return AllocHeapEx(heap, new_size);
    }

    size_t old_total = GetSize(SubVoidPtr(ptr, METADATA_LEADING));
    size_t old_payload = old_total - METADATA_TOTAL;

    if (new_size <= old_payload) {
        return ptr;
    }

    void* new_ptr = AllocHeapEx(heap, new_size);
    if (new_ptr == NULL) {
        return NULL;
    }
    memcpy(new_ptr, ptr, old_payload);
    FreeHeapEx(heap, ptr);
    return new_ptr;
}

export userexec size_t GetAllocationSizeEx(struct heap* heap, void* ptr) {
    (void) heap;
    return GetSize(SubVoidPtr(ptr, METADATA_LEADING)) - METADATA_TOTAL;
}

export userexec void InitHeapEx(struct heap* heap, void*(*get_memory)(size_t*), void(*return_memory)(void*, size_t)) {
    InitSpinlock(&heap->lock);
    heap->get_memory = get_memory;
    heap->return_memory = return_memory;
    memset(heap->_head_block, 0, sizeof(heap->_head_block));
    heap->_free_list_bitmap = 0;
}
