#pragma once

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <stdatomic.h>
#include <spinlock.h>
#include <objtypes.h>

/*
 * Objects: reference counted things with a cleanup function per type.
 *
 * Every kind of object is a real kernel object with its own OBJ_ type (objtypes.h), whether or not the kernel
 * itself does anything with it. Usermode never holds a pointer to one of them. It holds a pointer to a
 * `struct user_obj_ref` (an OBJ_USER_REF), which wraps whichever object it is a handle for, and is what usermode
 * Ref()s and Deref()s.
 */

#define OBJ_TYPE_SHIFT      (32 - OBJ_TYPE_BITS)
#define OBJ_REFCOUNT_MASK   ((1U << OBJ_TYPE_SHIFT) - 1)

static_assert(OBJ_LAST_TYPE < OBJ_MAX_TYPES, "too many object types for the bits in the header");

struct obj_header {
#ifndef NDEBUG
    char obj_sig[4];
#endif
    _Atomic uint32_t ref_count;
};

static inline uint8_t KeGetObjectType(void* _o) {
    struct obj_header* obj = _o;
    return obj->ref_count >> OBJ_TYPE_SHIFT;
}

static inline int KeGetRefCount(void* _o) {
    struct obj_header* obj = _o;
    return obj->ref_count & OBJ_REFCOUNT_MASK;
}

/* `cleanup_func` is called when the last reference goes. It is the one that frees the object. */
void KeRegisterObjectType(uint8_t type, void(*cleanup_func)(void*));

/* The new object starts with one reference, which belongs to the caller. */
void KeInitObject(void* obj, uint8_t type);
void KeRefObject(void* obj);
void KeDerefObject(void* obj);

/*
 * What usermode holds. It is a real kernel object (OBJ_USER_REF) that wraps any other object, so one object can
 * have several of these (different handles, and later different rights through each).
 *
 * The wrapper holds one kernel reference on `real_obj`, and usermode holds one kernel reference on the wrapper.
 * Both are given back when `user_ref` reaches zero, at which point the handle is dead: `user_gone` is set and
 * `real_obj` is NULL, and no new kernel reference can be taken to the object through it. Kernel code that is
 * using a user's object takes its own reference on the real object (not on the wrapper) and so keeps it alive
 * past that.
 *
 * `lock` protects `real_obj`, `user_ref` and `user_gone`. `permanent` is set (once, before usermode can see the
 * wrapper) on the handles to shared objects that live forever, such as the stock brushes: Ref() and Deref() of
 * these do nothing, so usermode can never free them.
 *
 * Later: the process this belongs to, ACLs, access rights.
 */
struct user_obj_ref {
    struct obj_header hdr;
    struct spinlock lock;
    void* real_obj;
    uint32_t user_ref  : 31;
    uint32_t user_gone : 1;
    bool permanent;
};

void KeInitUserRefType(void);

/*
 * Makes a handle for `obj`, with a user reference count of 1.
 *
 * This CONSUMES the caller's kernel reference on `obj`, success or failure, so it takes what a creation function
 * returns and hands it straight over (and to keep using `obj` the caller takes its own first). That also means
 * there is nothing to clean up on failure, which is ENOMEM (set as the last error), or EINVAL if `obj` is
 * itself a handle. A NULL `obj` gives NULL and sets nothing, so a creation function that failed can be passed
 * straight through.
 *
 * The result is what usermode is given. The kernel doesn't hold it, other than for the calls below.
 */
struct user_obj_ref* KeCreateUserRef(void* obj);

/* Call once on a new handle, before usermode can see it. The handle is then never freed, nor the object under it. */
void KeMarkUserRefPermanent(struct user_obj_ref* ref);

/*
 * Usermode's Ref() and Deref(). Both return 0 or an errno, and do nothing (and return 0) on a permanent handle.
 * EGONE is for one that has already had its last user reference dropped.
 *
 * KeUserDeref() may drop the user's own kernel reference to `ref`, so the caller needs one of its own, which is
 * what KeValidateUserRefAndAtomicallyRef() gives it.
 */
int KeUserRef(struct user_obj_ref* ref);
int KeUserDeref(struct user_obj_ref* ref);

/*
 * Checks that `handle` (an untrusted value from usermode) is a live handle, takes a kernel reference on the
 * object under it, and returns that object. If `type` isn't OBJ_ANYTYPE the object has to be of that type.
 * Returns NULL if any of that isn't so. The caller must KeDerefObject() the result when done.
 *
 * This is the first thing every system call that takes a handle does, and the result is the object itself (a
 * `struct brush*`, say) and not the handle, which is no use to anything but this file.
 *
 * Only the object is kept alive for the caller. The handle is checked, and its wrapper is held for exactly as
 * long as that takes, but if usermode closes the handle in the middle of the call the call carries on with
 * the object it has, as it has its own reference. (And later, any check of the handle's rights is made here.)
 */
void* KeValidateUserObjectAndAtomicallyRef(void* handle, int type);

/*
 * The same, but for the handle itself rather than the object under it, for the few calls that work on that:
 * Ref() and Deref(). It checks that this is a handle and nothing more, so it can still succeed on one that has
 * been closed (and they then report EGONE). Takes a kernel reference on the wrapper, to be given back with
 * KeDerefObject().
 */
struct user_obj_ref* KeValidateUserRefAndAtomicallyRef(void* handle);
