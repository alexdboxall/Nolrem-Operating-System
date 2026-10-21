#include <common.h>
#include <panic.h>
#include <log.h>
#include <string.h>
#include <heapex.h>
#include <spinlock.h>
#include <arch.h>
#include <vmm.h>

#define ALIGN       (sizeof(size_t))    // must be power of 2
#define BOOTSTRAP_HEAP_SIZE (1024 * 16)

static uint8_t bootstrap_heap[BOOTSTRAP_HEAP_SIZE];
static size_t bootstrap_heap_index = 0;
static struct spinlock heap_lock;

static struct heap kernel_heap;

static void* AllocBootstrapHeap(size_t bytes) {
    if (bootstrap_heap_index + bytes > BOOTSTRAP_HEAP_SIZE) {
        return NULL;
    }

    void* retv = bootstrap_heap + bootstrap_heap_index;
    bootstrap_heap_index += bytes;
    return retv;
}

export void* KeAllocHeap(size_t bytes) {
    if (bytes == 0) {
        return NULL;
    }
    AcquireSpinlock(&heap_lock);
    bytes = (bytes + ALIGN - 1) & ~(ALIGN - 1);
    void* retv = AllocHeapEx(&kernel_heap, bytes);
    ReleaseSpinlock(&heap_lock);
    if (retv == NULL) {
        Panic(PANIC_OUT_OF_HEAP);
    }
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
    if (retv == NULL) {
        Panic(PANIC_OUT_OF_HEAP);
    }
    return retv;
}

export void KeFreeHeap(void* ptr) {
    if (ptr == NULL) {
        return;
    }

    // for debugging
    size_t s = KeGetAllocationSize(ptr);
    memset(ptr, 0xCC, s);

    AcquireSpinlock(&heap_lock);
    FreeHeapEx(&kernel_heap, ptr);
    ReleaseSpinlock(&heap_lock);
}

void KernelHeapReturnMemory(void* ptr, size_t size) {
    size_t addr = (size_t) ptr;
    if ((addr & (PAGE_SIZE - 1)) != 0) {
        return;
    }
    if ((size & (PAGE_SIZE - 1)) != 0) {
        return;
    }
    FreeMemory(ptr, size);
}

void* KernelHeapRequestMemory(size_t* size) {
    *size = (*size + 1023) & ~1023;
    void* retv = AllocBootstrapHeap(*size);
    if (retv != NULL) {
        return retv;
    }
    retv = AllocAnonMemory(*size, VP_WRITE);
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
