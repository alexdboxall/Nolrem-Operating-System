#include <common.h>
#include <panic.h>
#include <log.h>
#include <string.h>
#include <heapex.h>
#include <spinlock.h>

#define ALIGN       (sizeof(size_t))    // must be power of 2
#define BOOTSTRAP_HEAP_SIZE (1024 * 8)

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

void* KernelHeapRequestMemory(size_t size) {
    return AllocBootstrapHeap(size);
}

void InitBootstrapHeap(void) {
    InitSpinlock(&heap_lock);
    InitHeapEx(&kernel_heap, KernelHeapRequestMemory);
}
