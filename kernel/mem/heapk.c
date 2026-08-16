#include <common.h>
#include <panic.h>
#include <log.h>
#include <string.h>
#include <heapex.h>
#include <spinlock.h>

#define ALIGN       (sizeof(size_t))    // must be power of 2
#define BOOTSTRAP_HEAP_SIZE (1024 * 32)

static uint8_t bootstrap_heap[BOOTSTRAP_HEAP_SIZE];
static bool use_real_heap = false;
static size_t bootstrap_heap_index = 0;
static struct spinlock heap_lock;

static size_t alloced_total = 0;

static struct heap kernel_heap;

static void* AllocBootstrapHeap(size_t bytes) {
    if (bootstrap_heap_index + bytes + sizeof(size_t) > BOOTSTRAP_HEAP_SIZE) {
        return NULL;
    }
    void* retv = bootstrap_heap + bootstrap_heap_index;
    ((size_t*) retv)[0] = bytes;
    alloced_total += bytes;
    bootstrap_heap_index += bytes + sizeof(size_t);
    return AddVoidPtr(retv, sizeof(size_t));
}

bool IsOnBootstrapHeap(void* ptr) {
    size_t addr = (size_t) ptr;
    return !use_real_heap || (addr >= (size_t) bootstrap_heap && addr < (size_t) (bootstrap_heap + BOOTSTRAP_HEAP_SIZE));
}

export void* KeAllocHeap(size_t bytes) {
    AcquireSpinlock(&heap_lock);

    bytes = (bytes + ALIGN - 1) & ~(ALIGN - 1);

    if (!use_real_heap) {
        void* retv = AllocBootstrapHeap(bytes);
        if (retv != NULL) {
            ReleaseSpinlock(&heap_lock);
            return retv;
        }

        /* Else fall through to real heap now */
        use_real_heap = true;
    }

    void* retv = AllocHeapEx(&kernel_heap, bytes);
    ReleaseSpinlock(&heap_lock);
    if (retv == NULL) {
        Panic(PANIC_OUT_OF_HEAP);
    }
    return retv;
}

export size_t KeGetAllocationSize(void* ptr) {
    if (IsOnBootstrapHeap(ptr)) {
        return *(((size_t*) ptr) - 1);
    } else {
        return GetAllocationSizeEx(&kernel_heap, ptr);
    }
}

static bool IsTopBoostrapHeapAllocation(void* ptr) {
    if (!IsOnBootstrapHeap(ptr)) {
        return false;
    }
    size_t addr = (size_t) ptr;
    size_t old_size = GetAllocationSize(ptr);
    return addr + old_size == (size_t) (bootstrap_heap + bootstrap_heap_index);
}

export void* KeReallocHeap(void* ptr, size_t new_size) {
    new_size = (new_size + ALIGN - 1) & ~(ALIGN - 1);

    size_t old_size = GetAllocationSize(ptr);
    if (old_size == new_size) {
        return ptr;
    }

    alloced_total += new_size;
    AcquireSpinlock(&heap_lock);

    if (IsOnBootstrapHeap(ptr)) {
        if (old_size > new_size) {
            if (IsTopBoostrapHeapAllocation(ptr)) {
                bootstrap_heap_index += new_size - old_size;
            }
            ((size_t*)ptr)[0] = new_size;
            ReleaseSpinlock(&heap_lock);
            return ptr;
        }
        
        if (IsTopBoostrapHeapAllocation(ptr)) {
            if (bootstrap_heap_index + new_size - old_size <= BOOTSTRAP_HEAP_SIZE) {
                bootstrap_heap_index += new_size - old_size;
                ((size_t*)ptr)[0] = new_size;
                ReleaseSpinlock(&heap_lock);
                return ptr;
            }
        }

        /* If we can't grow in place, then we just allocate a new area (which
         * might be on the regular heap) and copy the data over. No need to 
         * call FreeHeap here, as the old allocation is on boostrap heap and
         * we weren't able to grow it.
         */
        ReleaseSpinlock(&heap_lock);
        void* new_ptr = AllocHeap(new_size);
        memcpy(new_ptr, ptr, old_size);
        return new_ptr;
    }

    void* retv = ReallocHeapEx(&kernel_heap, ptr, new_size);
    ReleaseSpinlock(&heap_lock);
    if (retv == NULL) {
        Panic(PANIC_OUT_OF_HEAP);
    }
    return retv;
}

export void KeFreeHeap(void* ptr) {
    AcquireSpinlock(&heap_lock);

    if (IsOnBootstrapHeap(ptr)) {
        if (IsTopBoostrapHeapAllocation(ptr)) {
            size_t req_size = GetAllocationSize(ptr);
            bootstrap_heap_index -= sizeof(size_t) + req_size;
        }
        ReleaseSpinlock(&heap_lock);
        return;
    }

    FreeHeapEx(&kernel_heap, ptr);
    ReleaseSpinlock(&heap_lock);
}

void* KernelHeapRequestMemory(size_t size) {
    (void) size;
    return NULL;
}

void InitBoostrapHeap(void) {
    InitSpinlock(&heap_lock);
    InitHeapEx(&kernel_heap, KernelHeapRequestMemory);
}
