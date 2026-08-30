
#include "clipdraw_internal.h"

export void CdInit(void) {
    CdInitBrushSubsystem();
    CdInitPenSubsystem();
    CdInitUserRegionSubsystem();
    CdInitDcSubsystem();
    CdInitMouseSubsystem();
}