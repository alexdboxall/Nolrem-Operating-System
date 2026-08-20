#include <obj.h>
#include <common.h>
#include <log.h>

static void(*objtype_cleanup_handlers[32])(void*);
static void(*uobjtype_cleanup_handlers[32])(void*);

void RegisterObjectType(uint8_t type, void(*cleanup_func)(void*)) {
    objtype_cleanup_handlers[type] = cleanup_func;
}

void RegisterUserObjectType(uint8_t type, void(*cleanup_func)(void*)) {
    uobjtype_cleanup_handlers[type] = cleanup_func;
}

void InitObject(void* obj, uint8_t type) {
    struct obj_header* hdr = obj;
    atomic_store_explicit(&hdr->ref_count, 1, memory_order_relaxed);
    atomic_store_explicit(&hdr->objtype, type, memory_order_relaxed);
    atomic_thread_fence(memory_order_release);
}

export void RefObject(void* obj) {
    struct obj_header* hdr = obj;
    hdr->ref_count++;
}

export void DerefObject(void* obj) {
    struct obj_header* hdr = obj;
    if (--hdr->ref_count == 0) {
        objtype_cleanup_handlers[hdr->objtype](obj);
    }
}


static void CleanupUserObj(void* _obj) {
    struct user_obj_header* uo = _obj;
    uobjtype_cleanup_handlers[uo->user_type](uo);
}

void InitUserObjectType(void) {
    RegisterObjectType(OBJTYPE_USEROBJ, CleanupUserObj);
}

export pageable void InitUserObject(void* obj, uint8_t user_type) {
    InitObject(obj, OBJTYPE_USEROBJ);
    struct user_obj_header* uo = obj;
    InitSpinlock(&uo->lock);
    uo->user_gone = false;
    uo->user_ref_count = 1;
    uo->user_type = user_type;
}

export pageable void UserRef(void* obj) {
    struct user_obj_header* uo = obj;
    AcquireSpinlock(&uo->lock);
    uo->user_ref_count++;
    ReleaseSpinlock(&uo->lock);
}

export pageable void UserDeref(void* obj) {
    struct user_obj_header* uo = obj;
    AcquireSpinlock(&uo->lock);
    uo->user_ref_count--;
    if (uo->user_gone) {
        // Don't allow the user to trigger the 'userref == 0' part more than
        // once, as that will cause multiple, real dereferences!

    } else if (uo->user_ref_count == 0) {
        // TODO: user cleanup?
        uo->user_gone = true;
        DerefObject(obj);
    }
    ReleaseSpinlock(&uo->lock);
}

export void LockUserObject(void* obj) {
    struct user_obj_header* hdr = obj;
    AcquireSpinlock(&hdr->lock);
}

export void UnlockUserObject(void* obj) {
    struct user_obj_header* hdr = obj;
    ReleaseSpinlock(&hdr->lock);
}