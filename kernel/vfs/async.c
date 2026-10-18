
#include <transfer.h>
#include <string.h>
#include <errno.h>
#include <arch.h>
#include <log.h>
#include <mutex.h>
#include <assert.h>
#include <heap.h>
#include <msgbox.h>
#include <aio.h>
#include <vnode.h>

/* 
 Create (CreateIO) -> Queue -> Done (disk side calls SignalDoneIO) -> Complete (after WaitIO called)
 */


export void SignalDoneIO(struct aio* aio, int result_code) {
    aio->result_code = result_code;
    ReleaseMutex(&aio->mtx);
}

export int CreateIO(struct aio* aio, void* node_or_file, struct transfer tr, void(*callback)(void*), void* callback_ctxt) {
    InitStaticMutex(&aio->mtx);
    int res = AcquireMutex(&aio->mtx, TIMEOUT_INSTANT);
    (void) res; assert(res == 0);
    aio->transfer = tr;
    aio->completed = false;
    aio->node_or_file = node_or_file;
    aio->sub = NULL;
    RefObject(node_or_file);
    aio->callback = callback;
    aio->callback_ctxt = callback_ctxt;
    return 0;
}

/* 
 * Must be called before you read the result of the aio.
 * If trying to do sync io, call this straight after requesting.
 * If doing async io, call this when you're ready for the result.
 */
export int WaitIO(struct aio* aio, uint64_t timeout) {
    if (aio->completed) {
        return EALREADY;
    }
    int res;
    if (aio->sub != NULL) {
        res = WaitIO(aio->sub, timeout);
        if (res != 0) {
            goto cleanup_on_err;
        }
    }
    res = AcquireMutex(&aio->mtx, timeout);
    if (res != 0) {
        goto cleanup_on_err;
    }

    aio->completed = true;
    DerefObject(aio->node);
    DestroyStaticMutex(&aio->mtx);
    res = aio->result_code;
    if (aio->callback != NULL) {
        aio->callback(aio->callback_ctxt);
    }
    return res;

cleanup_on_err:
    aio->completed = true;
    DerefObject(aio->node);
    DestroyStaticMutex(&aio->mtx);
    return res;
}
