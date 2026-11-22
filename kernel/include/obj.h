#pragma once

#include <stdint.h>
#include <stddef.h>
#include <stdatomic.h>
#include <spinlock.h>

/* MAX 32 object types! */
#define OBJTYPE_SEM             0
#define OBJTYPE_VAS             1
#define OBJTYPE_PAGE_ORIGIN     2
#define OBJTYPE_PAGE_VIRT       3
#define OBJTYPE_THREAD          4
#define OBJTYPE_VNODE           5
#define OBJTYPE_FILE            6
#define OBJTYPE_MODULE          7
#define OBJTYPE_USEROBJ         8
#define OBJTYPE_MSGBOX          9
#define OBJTYPE_WAITCLOT        10
#define OBJTYPE_AIO             11
#define OBJTYPE_CPZ_INST        12
#define OBJTYPE_RWLOCK          13

struct obj_header {
#ifndef NDEBUG
    char obj_sig[4];
#endif 
    _Atomic uint32_t ref_count;
};

static inline uint8_t KeGetObjectType(void* _o) {
    struct obj_header* obj = _o;
    return obj->ref_count >> 27;
}

static inline int KeGetRefCount(void* _o) {
    struct obj_header* obj = _o;
    return obj->ref_count & 0x7FFFFFF;
}

struct user_obj_header {
    struct obj_header hdr;
    uint32_t user_ref_count  : 24;
    uint32_t user_gone       : 1;
    uint32_t user_type       : 7;
    struct spinlock lock;

    /*
     * Set (once, by whoever created the object, before usermode can see it) on
     * objects that are shared and live forever, such as the stock brushes.
     * Usermode Ref()/Deref() on these does nothing, so it can never free them.
     */
    uint8_t user_permanent;
};

void KeRegisterObjectType(uint8_t type, void(*cleanup_func)(void*));
void KeInitObject(void* obj, uint8_t type);
void KeRefObject(void* obj);
void KeDerefObject(void* obj);

void KeRegisterUserObjectType(uint8_t type, void(*cleanup_func)(void*));
void KeInitUserObjectType(void);
void KeInitUserObject(void* obj, uint8_t user_type);
void KeLockUserObject(void* obj);
void KeUnlockUserObject(void* obj);
void KeMarkUserObjectPermanent(void* obj);

int KeUserRef(void* obj);
int KeUserDeref(void* obj);