
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
 Create (KeCreateIO) -> Queue -> Done (disk side calls KeSignalDoneIO) -> Complete (after KeWaitIO called)
 */


export void KeSignalDoneIO(struct aio* aio, int result_code) {
    aio->result_code = result_code;
    KeReleaseMutex(&aio->mtx);
}

export int KeCreateTruncateIO(struct aio* aio, void* node_or_file, off_t pos, void(*callback)(void*), void* callback_ctxt) {
    KeInitStaticMutex(&aio->mtx);
    int res = KeAcquireMutex(&aio->mtx, TIMEOUT_INSTANT);
    (void) res; assert(res == 0);
    aio->operation = AIO_OP_TRUNCATE;
    aio->completed = false;
    aio->node_or_file = node_or_file;
    aio->sub = NULL;
    aio->args.truncate.size = pos;
    KeRefObject(node_or_file);
    aio->callback = callback;
    aio->callback_ctxt = callback_ctxt;
    return 0;
}

export int KeCreateIO(struct aio* aio, void* node_or_file, struct transfer tr, void(*callback)(void*), void* callback_ctxt) {
    KeInitStaticMutex(&aio->mtx);
    int res = KeAcquireMutex(&aio->mtx, TIMEOUT_INSTANT);
    (void) res; assert(res == 0);
    aio->transfer = tr;
    aio->operation = tr.direction == TRANSFER_WRITE ? AIO_OP_WRITE : AIO_OP_READ;
    aio->completed = false;
    aio->node_or_file = node_or_file;
    aio->sub = NULL;
    KeRefObject(node_or_file);
    aio->callback = callback;
    aio->callback_ctxt = callback_ctxt;
    return 0;
}

/* 
 * Must be called before you read the result of the aio.
 * If trying to do sync io, call this straight after requesting.
 * If doing async io, call this when you're ready for the result.
 *
 * Don't pass a finite timeout (yet). If it expires, this gives up on the aio
 * (it is marked completed and its mutex is destroyed) but the driver still has
 * it queued, and will later write the result into it and signal it. For the
 * usual aio on the caller's stack, that is a write into a dead stack frame. Until
 * an aio can outlive its waiter (heap allocated and reference counted), a
 * stuck device is dealt with by the driver, which has to time out its own
 * hardware waits and complete the aio with an error, as the floppy driver does.
 */
export int KeWaitIO(struct aio* aio, uint64_t timeout) {
    if (aio->completed) {
        return EALREADY;
    }
    int res;
    if (aio->sub != NULL) {
        res = KeWaitIO(aio->sub, timeout);
        if (res != 0) {
            goto cleanup_on_err;
        }
    }
    res = KeAcquireMutex(&aio->mtx, timeout);
    if (res != 0) {
        goto cleanup_on_err;
    }

    aio->completed = true;
    KeDerefObject(aio->node);
    KeDestroyStaticMutex(&aio->mtx);
    res = aio->result_code;
    if (aio->callback != NULL) {
        aio->callback(aio->callback_ctxt);
    }
    return res;

cleanup_on_err:
    aio->completed = true;
    KeDerefObject(aio->node);
    KeDestroyStaticMutex(&aio->mtx);
    return res;
}
