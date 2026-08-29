
#include <obj.h>
#include <heap.h>
#include <log.h>
#include <mutex.h>
#include "winmgr_internal.h"

#define DC_CACHE_SIZE   8
struct dc_cache_entry {
    struct dc* dc;
    bool allocated;
};

static struct mutex* dc_cache_mtx;
static struct dc_cache_entry dc_cache[DC_CACHE_SIZE];

export struct dc* WmGetDC(void) {
    int res = AcquireMutex(dc_cache_mtx, TIMEOUT_INFINITE);
    if (res != 0) {
        return NULL;
    }

    for (int i = 0 ; i < DC_CACHE_SIZE; ++i) {
        if (!dc_cache[i].allocated) {
            dc_cache[i].allocated = true;
            struct dc* dc = dc_cache[i].dc;
            ReleaseMutex(dc_cache_mtx);
            return dc;
        }
    }

    ReleaseMutex(dc_cache_mtx);
    return CdCreateDc();
}

export int WmReturnDC(struct dc* dc) {
    int res = AcquireMutex(dc_cache_mtx, TIMEOUT_INFINITE);
    if (res != 0) {
        return res;
    }

    for (int i = 0 ; i < DC_CACHE_SIZE; ++i) {
        if (dc_cache[i].dc == dc) {
            dc_cache[i].allocated = false;
            // We reset the DC on return, instead of Get(), because this will
            // often allow e.g. a brush or pen set by the user of the DC to be
            // properly released and cleaned up (instead of waiting for the 
            // next Get() for it to be cleaned up).
            CdResetDC(dc);
            ReleaseMutex(dc_cache_mtx);
            return 0;
        }
    }

    ReleaseMutex(dc_cache_mtx);
    DerefObject(dc);
    return 0;
}

void WmInitDcCache(void) {
    for (int i = 0; i < DC_CACHE_SIZE; ++i) {
        dc_cache[i].dc = CdCreateDc();
        dc_cache[i].allocated = false;
    }
    dc_cache_mtx = CreateMutex();
}
