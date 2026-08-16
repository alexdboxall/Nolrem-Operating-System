#include <obj.h>
#include "clipdraw_internal.h"

static bool IsValidUserOwnedObject(void* obj) {
    // TODO: check if this process has a valid user object with the same
    // address in the list.
    (void) obj;
    return true;
}

bool ValidateUserRegionAndAtomicallyRef(struct uregion* ur) {
    bool ok = IsValidUserOwnedObject(ur) && ur->hdr.user_type == UOBJ_REGION;
    if (ok) {
        RefObject(ur);
    }
    return ok;
}

bool ValidateUserObjectAndAtomicallyRef(void* obj) {
    bool ok = IsValidUserOwnedObject(obj);
    if (ok) {
        RefObject(obj);
    }
    return ok;
}