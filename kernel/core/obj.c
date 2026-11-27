#include <obj.h>
#include <common.h>
#include <string.h>
#include <log.h>
#include <errno.h>

static void(*objtype_cleanup_handlers[OBJ_MAX_TYPES])(void*);

export void KeRegisterObjectType(uint8_t type, void(*cleanup_func)(void*)) {
    objtype_cleanup_handlers[type] = cleanup_func;
}

export void KeInitObject(void* obj, uint8_t type) {
    struct obj_header* hdr = obj;
#ifndef NDEBUG
    memcpy(hdr->obj_sig, "OBJ:", 4);
#endif
    atomic_store_explicit(&hdr->ref_count, 1U | ((uint32_t) type << OBJ_TYPE_SHIFT), memory_order_relaxed);
    atomic_thread_fence(memory_order_release);
}

export void KeRefObject(void* obj) {
    struct obj_header* hdr = obj;
    hdr->ref_count++;
}

export void KeDerefObject(void* obj) {
    struct obj_header* hdr = obj;
    if (((--hdr->ref_count) & OBJ_REFCOUNT_MASK) == 0) {
        objtype_cleanup_handlers[hdr->ref_count >> OBJ_TYPE_SHIFT](obj);
    }
}
