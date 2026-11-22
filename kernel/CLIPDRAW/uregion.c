#include <obj.h>
#include <heap.h>
#include "clipdraw_internal.h"
#include "api.h"

static void CdCleanupUserRegion(void* _ur) {
    struct uregion* ur = _ur;
    if (ur == NULL) {
        return;
    }
    CdFreeRegion(ur->rgn);
    KeFreeHeap(ur);
}

// Transfers the 'ownership' of the region to the uregion.
// ie. we don't call FreeRegion() on the original region after this
struct uregion* CdRegionToUserRegion(struct region rgn) {
    struct uregion* ur = KeAllocHeap(sizeof(struct uregion));
    if (ur == NULL) {
        return NULL;
    }
    KeInitUserObject(ur, UOBJ_REGION);
    ur->rgn = rgn;
    return ur;
}

// No transfer of ownership, nor changing of ref count happens
struct region CdUserRegionToRegion(struct uregion* urgn) {
    return urgn->rgn;
}
    
void CdInitUserRegionSubsystem(void) {
    KeRegisterUserObjectType(UOBJ_REGION, CdCleanupUserRegion);
}