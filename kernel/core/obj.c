#include <obj.h>
#include <common.h>
#include <string.h>
#include <log.h>
#include <errno.h>

static void(*objtype_cleanup_handlers[32])(void*);
static void(*uobjtype_cleanup_handlers[32])(void*);

void KeRegisterObjectType(uint8_t type, void(*cleanup_func)(void*)) {
    objtype_cleanup_handlers[type] = cleanup_func;
}

export void KeRegisterUserObjectType(uint8_t type, void(*cleanup_func)(void*)) {
    uobjtype_cleanup_handlers[type] = cleanup_func;
}

void KeInitObject(void* obj, uint8_t type) {
    struct obj_header* hdr = obj;
#ifndef NDEBUG
    memcpy(hdr->obj_sig, "OBJ:", 4);
#endif 
    atomic_store_explicit(&hdr->ref_count, 1U | ((uint32_t) type << 27U), memory_order_relaxed);
    atomic_thread_fence(memory_order_release);
}

export void KeRefObject(void* obj) {
    struct obj_header* hdr = obj;
    hdr->ref_count++;
}

export void KeDerefObject(void* obj) {
    struct obj_header* hdr = obj;
    if (((--hdr->ref_count) & 0x7FFFFFF) == 0) {
        objtype_cleanup_handlers[hdr->ref_count >> 27](obj);
    }
}

static void KeCleanupUserObj(void* _obj) {
    struct user_obj_header* uo = _obj;
    uobjtype_cleanup_handlers[uo->user_type](uo);
}

void KeInitUserObjectType(void) {
    KeRegisterObjectType(OBJTYPE_USEROBJ, KeCleanupUserObj);
}

export void KeInitUserObject(void* obj, uint8_t user_type) {
    KeInitObject(obj, OBJTYPE_USEROBJ);
    struct user_obj_header* uo = obj;
    KeInitSpinlock(&uo->lock);
    uo->user_gone = false;
    uo->user_ref_count = 1;
    uo->user_type = user_type;
    uo->user_permanent = false;
}

export void KeMarkUserObjectPermanent(void* obj) {
    struct user_obj_header* uo = obj;
    uo->user_permanent = true;
}

export pageable int KeUserRef(void* obj) {
    struct user_obj_header* uo = obj;
    if (uo->user_permanent) {
        return 0;
    }
    KeAcquireSpinlock(&uo->lock);
    if (uo->user_ref_count == 0xFFFFFF) {
        KeReleaseSpinlock(&uo->lock);
        return EOVERFLOW;
    }
    if (uo->user_ref_count == 0) {
        KeReleaseSpinlock(&uo->lock);
        return EGONE;
    }
    uo->user_ref_count++;
    KeReleaseSpinlock(&uo->lock);
    return 0;
}

pageable int KeUserDeref(void* obj) {
    struct user_obj_header* uo = obj;
    if (uo->user_permanent) {
        return 0;
    }
    KeAcquireSpinlock(&uo->lock);
    if (uo->user_ref_count == 0) {
        KeReleaseSpinlock(&uo->lock);
        return EGONE;
    }
    uo->user_ref_count--;
    int retv = 0;
    if (uo->user_gone) {
        // Don't allow the user to trigger the 'userref == 0' part more than
        // once, as that will cause multiple, real dereferences!
        retv = EGONE;
    } else if (uo->user_ref_count == 0) {
        // TODO: user cleanup?
        uo->user_gone = true;
        KeDerefObject(obj);
    }
    KeReleaseSpinlock(&uo->lock);
    return retv;
}

export void KeLockUserObject(void* obj) {
    struct user_obj_header* hdr = obj;
    KeAcquireSpinlock(&hdr->lock);
}

export void KeUnlockUserObject(void* obj) {
    struct user_obj_header* hdr = obj;
    KeReleaseSpinlock(&hdr->lock);
}