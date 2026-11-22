
#include "clipdraw_internal.h"
#include <reclaim.h>

export void CdInit(void) {
    /* Cached scaled glyphs can always be regenerated, so give them back when memory is tight. */
    KeRegisterReclaimCallback(CdPurgeScaledGlyphCacheIfNeeded);
    CdInitSyscalls();
    CdInitMouseSubsystem();
    CdInitBrushSubsystem();
    CdInitPenSubsystem();
    CdInitUserRegionSubsystem();
    CdInitDcSubsystem();
    CdInitTypefaceSubsystem();
    
    extern void VgaInit();
    VgaInit();
}