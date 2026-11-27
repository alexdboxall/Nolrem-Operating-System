#include <obj.h>
#include <common.h>
#include <heap.h>
#include <errno.h>
#include <syscall.h>

#define USER_REF_MAX    0x7FFFFFFFU

static void KeCleanupUserRef(void* _ref) {
    struct user_obj_ref* ref = _ref;
    /* Normally it has long since been given back by the last user Deref(), but a handle that is torn down some
       other way (later: its process dying) still owns the object. */
    if (ref->real_obj != NULL) {
        KeDerefObject(ref->real_obj);
    }
    KeFreeHeap(ref);
}

void KeInitUserRefType(void) {
    KeRegisterObjectType(OBJ_USER_REF, KeCleanupUserRef);
}

export pageable struct user_obj_ref* KeCreateUserRef(void* obj) {
    if (obj == NULL) {
        return NULL;
    }
    if (KeGetObjectType(obj) == OBJ_USER_REF) {
        /* A handle to a handle would be a way to hold the object that Deref() of the original can't take away. */
        KeDerefObject(obj);
        KeSetLastUserError(EINVAL);
        return NULL;
    }

    struct user_obj_ref* ref = KeAllocHeap(sizeof(struct user_obj_ref));
    if (ref == NULL) {
        KeDerefObject(obj);
        KeSetLastUserError(ENOMEM);
        return NULL;
    }
    KeInitObject(ref, OBJ_USER_REF);
    KeInitSpinlock(&ref->lock);
    ref->real_obj = obj;            /* the caller's reference becomes this one */
    ref->user_ref = 1;
    ref->user_gone = 0;
    ref->permanent = false;
    return ref;
}

export void KeMarkUserRefPermanent(struct user_obj_ref* ref) {
    ref->permanent = true;
}

export pageable int KeUserRef(struct user_obj_ref* ref) {
    if (ref->permanent) {
        return 0;
    }
    int retv = 0;
    KeAcquireSpinlock(&ref->lock);
    if (ref->user_gone) {
        retv = EGONE;
    } else if (ref->user_ref == USER_REF_MAX) {
        retv = EOVERFLOW;
    } else {
        ref->user_ref++;
    }
    KeReleaseSpinlock(&ref->lock);
    return retv;
}

export pageable int KeUserDeref(struct user_obj_ref* ref) {
    if (ref->permanent) {
        return 0;
    }
    void* real = NULL;
    KeAcquireSpinlock(&ref->lock);
    if (ref->user_gone) {
        /* Don't allow this to run the 'user ref is zero' part more than once, as that gives
           back the references that usermode holds, and they only exist once! */
        KeReleaseSpinlock(&ref->lock);
        return EGONE;
    }
    if (--ref->user_ref == 0) {
        ref->user_gone = 1;
        real = ref->real_obj;
        ref->real_obj = NULL;
    }
    KeReleaseSpinlock(&ref->lock);

    if (real != NULL) {
        /* Not under the lock, as either of these may be the last reference to something. The wrapper itself
           is safe to touch until the second one, because the caller holds a reference to it. */
        KeDerefObject(real);
        KeDerefObject(ref);
    }
    return 0;
}

static bool KeIsValidUserRef(void* handle) {
    // TODO: check if this process has a handle at this address in its list of them. That has to be what
    // makes `handle` safe to read below, so it has to be done first, and (until the reference is taken)
    // under the lock on that list, or the handle could be closed and freed in between.
    (void) handle;
    return true;
}

export struct user_obj_ref* KeValidateUserRefAndAtomicallyRef(void* handle) {
    // TODO: need to prevent any changes to the process list between here...
    /* Don't dereference NULL below to read the type. */
    if (handle == NULL || !KeIsValidUserRef(handle)) {
        return NULL;
    }
    /* Whatever else the pointer is (a raw object, as in the days when that was the handle), it isn't one. */
    if (KeGetObjectType(handle) != OBJ_USER_REF) {
        return NULL;
    }
    KeRefObject(handle);
    // ...and here
    return handle;
}

export void* KeValidateUserObjectAndAtomicallyRef(void* handle, int type) {
    struct user_obj_ref* ref = KeValidateUserRefAndAtomicallyRef(handle);
    if (ref == NULL) {
        return NULL;
    }

    /* The real object is only ours to take a reference to while the handle is open, as that is what holds
       one. Both that check and the reference have to happen under the lock, or the last Deref() could
       slip in between them. */
    void* real = NULL;
    KeAcquireSpinlock(&ref->lock);
    if (!ref->user_gone && (type == OBJ_ANYTYPE || KeGetObjectType(ref->real_obj) == type)) {
        real = ref->real_obj;
        KeRefObject(real);
    }
    KeReleaseSpinlock(&ref->lock);

    KeDerefObject(ref);
    return real;
}
