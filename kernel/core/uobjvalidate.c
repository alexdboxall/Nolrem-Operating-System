#include <common.h>
#include <obj.h>
#include <uobj.h>

static bool KeIsValidUserOwnedObject(void* obj) {
    // TODO: check if this process has a valid user object with the same
    // address in the list.
    (void) obj;
    return true;
}

export bool KeValidateUserObjectAndAtomicallyRef(void* obj, int type) {
    /* Don't dereference NULL below to read the type. */
    if (obj == NULL) {
        return false;
    }
    // TODO: need to prevent any changes to the process list between here...
    bool ok = KeIsValidUserOwnedObject(obj);
    if (type != UOBJ_ANYTYPE) {
        ok = ok && ((struct user_obj_header*) obj)->user_type == type;
    }
    if (ok) {
        KeRefObject(obj);
    }
    // ...and here
    return ok;
}