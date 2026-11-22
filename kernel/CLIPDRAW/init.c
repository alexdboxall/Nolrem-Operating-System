
#include "clipdraw_internal.h"
#include <reclaim.h>

export void CdInit(void) {
    /* Cached scaled glyphs can always be regenerated, so give them back when memory is tight. */
    CdInitSyscalls();
    CdInitMouseSubsystem();
    CdInitBrushSubsystem();
    CdInitPenSubsystem();
    CdInitUserRegionSubsystem();
    CdInitDcSubsystem();
    CdInitTypefaceSubsystem();
    
    KeRegisterReclaimCallback(CdPurgeScaledGlyphCacheIfNeeded);

    extern void VgaInit();
    VgaInit();
}