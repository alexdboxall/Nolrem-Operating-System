#include <common.h>
#include <panic.h>
#include <log.h>
#include <string.h>
#include <heapex.h>
#include <spinlock.h>
#include <phys.h>
#include <arch.h>
#include <vmm.h>
#include <scheduler.h>          // IsSchedulingInitialised

#define ALIGN       (sizeof(size_t))    // must be power of 2

static uint8_t bootstrap_heap[PAGE_SIZE];
static bool given_bootstrap_heap = false;
static struct spinlock heap_lock;
static struct heap kernel_heap;

export void* KeAllocHeap(size_t bytes) {
    if (bytes == 0) {
        return NULL;
    }
    AcquireSpinlock(&heap_lock);
    if (IsOutOfMemory() && IsSchedulingInitialised()) {
        ReleaseSpinlock(&heap_lock);
        return NULL;
    }
    bytes = (bytes + ALIGN - 1) & ~(ALIGN - 1);
    void* retv = AllocHeapEx(&kernel_heap, bytes);
    ReleaseSpinlock(&heap_lock);
    LogPrintf("KeAllocHeap: 0x%X (bytes 0x%X)\n", retv, bytes);
    return retv;
}

void* AllocPermanentHeap(size_t bytes) {
    static size_t sum = 0;
    sum += bytes;
    LogPrintf("Alloc 0x%X bytes permanently... sum = 0x%X\n", bytes, sum);
    return KeAllocHeap(bytes);
}

export size_t KeGetAllocationSize(void* ptr) {
    return GetAllocationSizeEx(&kernel_heap, ptr);
}

export void KePurgeFreeAreasFromHeap(void) {
    AcquireSpinlock(&heap_lock);
    PurgeFreeAreasFromHeapEx(&kernel_heap);
    ReleaseSpinlock(&heap_lock);
}

export void* KeReallocHeap(void* ptr, size_t new_size) {
    if (ptr == NULL) {
        return KeAllocHeap(new_size);
    }
    if (new_size == 0) {
        KeFreeHeap(ptr);
        return NULL;
    }

    AcquireSpinlock(&heap_lock);
    void* retv = ReallocHeapEx(&kernel_heap, ptr, new_size);
    ReleaseSpinlock(&heap_lock);
    return retv;
}

export void KeFreeHeap(void* ptr) {
    if (ptr == NULL) {
        return;
    }

    // for debugging
    size_t s = KeGetAllocationSize(ptr);
    memset(ptr, 0xCC, s);

    LogPrintf("KeFreeHeap: 0x%X\n", ptr);

    AcquireSpinlock(&heap_lock);
    FreeHeapEx(&kernel_heap, ptr);
    ReleaseSpinlock(&heap_lock);
}

void KernelHeapReturnMemory(void* ptr, size_t size) {
    if ((uintptr_t) ptr == (uintptr_t) bootstrap_heap) {
        return;
    }
    size_t addr = (size_t) ptr;
    if ((addr & (PAGE_SIZE - 1)) != 0) {
        return;
    }
    if ((size & (PAGE_SIZE - 1)) != 0) {
        return;
    }
    LogPrintf("Returning: ptr=0x%X, size=0x%X\n", ptr, size);
    FreeMemory(ptr, size);
}

void* KernelHeapRequestMemory(size_t* size) {
    if (!given_bootstrap_heap) {
        given_bootstrap_heap = true;
        *size = sizeof(bootstrap_heap);
        return bootstrap_heap;
    }
    void* retv = AllocAnonMemory(*size, VP_WRITE);
    *size = (*size + PAGE_SIZE - 1) & (~(PAGE_SIZE - 1));
    return retv;
}

void InitBootstrapHeap(void) {
    memset(bootstrap_heap, 0xEE, sizeof(bootstrap_heap));
    InitSpinlock(&heap_lock);
    InitHeapEx(&kernel_heap, KernelHeapRequestMemory, KernelHeapReturnMemory);
}

size_t GetHeapAllocatedAmount(void) {
    AcquireSpinlock(&heap_lock);
    size_t retv = kernel_heap.outstanding_allocated_memory;
    ReleaseSpinlock(&heap_lock);
    return retv;
}
