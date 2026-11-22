
#include <obj.h>
#include <heap.h>
#include <log.h>
#include <mutex.h>
#include <panic.h>
#include <errno.h>
#include "winmgr_internal.h"

#define DC_CACHE_SIZE   8
struct dc_cache_entry {
    struct dc* dc;
    bool allocated;
};

static struct mutex* dc_cache_mtx;
static struct dc_cache_entry dc_cache[DC_CACHE_SIZE];

export struct dc* WmGetDC(void) {
    int res = KeAcquireMutex(dc_cache_mtx, TIMEOUT_INFINITE);
    if (res != 0) {
        return NULL;
    }

    for (int i = 0 ; i < DC_CACHE_SIZE; ++i) {
        if (!dc_cache[i].allocated && dc_cache[i].dc != NULL) {
            dc_cache[i].allocated = true;
            struct dc* dc = dc_cache[i].dc;
            KeReleaseMutex(dc_cache_mtx);
            return dc;
        }
    }

    KeReleaseMutex(dc_cache_mtx);
    return CdCreateDc();
}

export int WmReturnDC(struct dc* dc) {
    if (dc == NULL) {
        return EINVAL;
    }
    int res = KeAcquireMutex(dc_cache_mtx, TIMEOUT_INFINITE);
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
            KeReleaseMutex(dc_cache_mtx);
            return 0;
        }
    }

    KeReleaseMutex(dc_cache_mtx);
    KeDerefObject(dc);
    return 0;
}

void WmInitDcCache(void) {
    dc_cache_mtx = KeCreateMutex();
    if (dc_cache_mtx == NULL) {
        KePanic(PANIC_OUT_OF_MEMORY);       /* boot time; every DC goes through this lock */
    }
    for (int i = 0; i < DC_CACHE_SIZE; ++i) {
        /* If this fails the slot stays empty (NULL): WmGetDC skips it and makes a DC on demand instead. */
        dc_cache[i].dc = CdCreateDc();
        dc_cache[i].allocated = false;
    }
}
