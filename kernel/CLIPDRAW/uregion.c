#include <obj.h>
#include <heap.h>
#include <errno.h>
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
// On failure this still takes ownership: the region is freed, so callers never have anything to clean up.
// A 'bad' region means whatever made it ran out of memory, so that is reported rather than handed out as a handle.
struct uregion* CdRegionToUserRegion(struct region rgn) {
    if (CdIsRegionBad(rgn)) {
        KeSetLastUserError(ENOMEM);
        return NULL;
    }
    struct uregion* ur = KeAllocHeap(sizeof(struct uregion));
    if (ur == NULL) {
        CdFreeRegion(rgn);
        KeSetLastUserError(ENOMEM);
        return NULL;
    }
    KeInitObject(ur, OBJ_REGION);
    ur->rgn = rgn;
    return ur;
}

// The same, but then gives the new region object its handle for usermode, which is what a system call wants.
// So this makes the one thing that usermode holds, and the object under it is only kept alive by the handle.
// Fails (ENOMEM, as above) for a bad region, or if there is no memory for the handle.
struct user_obj_ref* CdRegionToUserHandle(struct region rgn) {
    return KeCreateUserRef(CdRegionToUserRegion(rgn));
}

// No transfer of ownership, nor changing of ref count happens
struct region CdUserRegionToRegion(struct uregion* urgn) {
    return urgn->rgn;
}
    
void CdInitUserRegionSubsystem(void) {
    KeRegisterObjectType(OBJ_REGION, CdCleanupUserRegion);
}