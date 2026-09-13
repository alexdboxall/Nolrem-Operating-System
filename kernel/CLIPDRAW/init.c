
#include "clipdraw_internal.h"

export void CdInit(void) {
    CdInitMouseSubsystem();
    CdInitBrushSubsystem();
    CdInitPenSubsystem();
    CdInitUserRegionSubsystem();
    CdInitDcSubsystem();

    extern void InitVga();
    InitVga();
}