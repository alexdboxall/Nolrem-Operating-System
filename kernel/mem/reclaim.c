#include <common.h>
#include <reclaim.h>
#include <errno.h>
#include <heap.h>
#include <vfs.h>
#include <vmm.h>
#include <spinlock.h>

#define MAX_RECLAIM_CALLBACKS   8

static struct spinlock reclaim_lock;
static ke_reclaim_callback_t callbacks[MAX_RECLAIM_CALLBACKS];
static int num_callbacks = 0;

export int KeRegisterReclaimCallback(ke_reclaim_callback_t callback) {
    if (callback == NULL) {
        return EINVAL;
    }
    int res = 0;
    KeAcquireSpinlock(&reclaim_lock);
    if (num_callbacks >= MAX_RECLAIM_CALLBACKS) {
        res = ENOSPC;
    } else {
        callbacks[num_callbacks++] = callback;
    }
    KeReleaseSpinlock(&reclaim_lock);
    return res;
}

export void KeReclaimMemory(void) {
    KeDiscardPage();
    KeDiscardPage();

    /*
     * Registration only ever appends, so a snapshot of the count is enough to
     * walk the table without holding the lock while the callbacks run.
     */
    KeAcquireSpinlock(&reclaim_lock);
    int count = num_callbacks;
    KeReleaseSpinlock(&reclaim_lock);
    for (int i = 0; i < count; ++i) {
        callbacks[i]();
    }

    KePurgeOldVnodes();
    KePurgeFreeAreasFromHeap();
}
