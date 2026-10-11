
#include <transfer.h>
#include <string.h>
#include <errno.h>
#include <arch.h>
#include <log.h>
#include <mutex.h>
#include <assert.h>
#include <heap.h>
#include <msgbox.h>

/* 
 Create (CreateIO) -> Queue -> Done (disk side calls SignalDoneIO) -> Complete (after WaitIO called)
 */
struct aio {
    struct obj_header hdr;
    struct mutex mtx;   // starts acquired. disk releases when i/o is completed
    struct msg msg_on_done;
    struct msgbox* mbox_on_done;
    struct transfer transfer;
    int result_code;
    bool completed;
};

export void SignalDoneIO(struct aio* aio, int result_code) {
    aio->result_code = result_code;
    if (aio->mbox_on_done) {
        // DeferIrqSend(aio->mbox_on_done, aio->msg_on_done)
    }
    ReleaseMutex(&aio->mtx);
}

export int CreateIO(struct aio* recycle_me, struct transfer tr, struct msgbox* mbox_on_done, struct msg msg_on_done) {
    if (recycle_me == NULL) {
        recycle_me = AllocHeap(sizeof(struct aio));
    } else {
        if (recycle_me->hdr.ref_count != 1) {
            return EINVAL;
        }
        DestroyStaticMutex(&recycle_me->mtx);
    }

    InitObject(recycle_me, OBJTYPE_AIO);
    InitStaticMutex(&recycle_me->mtx);
    int res = AcquireMutex(&recycle_me->mtx, TIMEOUT_INSTANT);
    (void) res; assert(res == 0);
    recycle_me->transfer = tr;
    recycle_me->completed = false;
    recycle_me->mbox_on_done = mbox_on_done;
    recycle_me->msg_on_done = msg_on_done;
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
    int res = AcquireMutex(&aio->mtx, timeout);
    if (res != 0) {
        return res;
    }
    aio->completed = true;
    return aio->result_code;
}
