#include <obj.h>
#include "../clipdraw_internal.h"

static bool IsValidUserOwnedObject(void* obj) {
    // TODO: check if this process has a valid user object with the same
    // address in the list.
    (void) obj;
    return true;
}

bool ValidateUserObjectAndAtomicallyRef(void* obj, int type) {
    bool ok = IsValidUserOwnedObject(obj);
    if (type != UOBJ_ANYTYPE) {
        ok = ok && ((struct user_obj_header*) obj)->user_type == type;
    }
    if (ok) {
        RefObject(obj);
    }
    return ok;
}