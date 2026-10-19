
#include "clipdraw_internal.h"

export void CdInit(void) {
    CdInitMouseSubsystem();
    CdInitBrushSubsystem();
    CdInitPenSubsystem();
    CdInitUserRegionSubsystem();
    CdInitDcSubsystem();
    CdInitTypefaceSubsystem();
    
    extern void InitVga();
    InitVga();
}