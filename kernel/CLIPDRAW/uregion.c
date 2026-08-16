#include <obj.h>
#include <heap.h>
#include "clipdraw_internal.h"
#include "api.h"

static void CleanupUserRegion(void* _ur) {
    struct uregion* ur = _ur;
    CdFreeRegion(ur->rgn);
    KeFreeHeap(ur);
}

// Transfers the 'ownership' of the region to the uregion.
// ie. we don't call FreeRegion() on the original region after this
struct uregion* RegionToUserRegion(struct region rgn) {
    struct uregion* ur = KeAllocHeap(sizeof(struct uregion));
    InitUserObject(ur, UOBJ_REGION);
    ur->rgn = rgn;
    return ur;
}

// No transfer of ownership, nor changing of ref count happens
struct region UserRegionToRegion(struct uregion* urgn) {
    return urgn->rgn;
}
    
void CdInitUserRegionSubsystem(void) {
    RegisterUserObjectType(UOBJ_REGION, CleanupUserRegion);
}