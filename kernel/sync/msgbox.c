#include <obj.h>
#include <heap.h>
#include <common.h>
#include <sem.h>
#include <errno.h>
#include <thread.h>
#include <scheduler.h>
#include <string.h>
#include <log.h>
#include <msgbox.h>

struct msgbox {
    struct obj_header hdr;
    uint8_t* data;
    size_t message_size;
    size_t max_count;
    size_t start_idx;
    size_t end_idx;
    struct mutex* lock;
    struct sem* empty_sem;
    struct sem* filled_sem;
    int count;
};

static void CleanupMsgbox(void* _mbox) {
    struct msgbox* mbox = _mbox;
    FreeHeap(mbox->data);
    FreeHeap(mbox);
}

void InitMessageBox(void) {
    RegisterObjectType(OBJTYPE_MSGBOX, CleanupMsgbox);
}

export struct msgbox* CreateMessageBox(size_t message_size, size_t max_count) {
    struct msgbox* mbox = AllocHeap(sizeof(struct msgbox));
    InitObject(mbox, OBJTYPE_MSGBOX);
    mbox->message_size = message_size;
    mbox->max_count = max_count;
    mbox->data = AllocHeap(message_size * max_count);
    mbox->start_idx = 0;
    mbox->end_idx = 0;
    mbox->lock = CreateMutex();
    mbox->empty_sem = CreateSem(max_count, 0);
    mbox->filled_sem = CreateSem(max_count, max_count);
    mbox->count = 0;
    return mbox;
}

export int KePostMessage(struct msgbox* mbox, const void* msg, int64_t timeout) {
    if (mbox == NULL || msg == NULL) {
        return EINVAL;
    }

    int res = AcquireSem(mbox->empty_sem, timeout);
    if (res != 0) {
        return res;
    }

    res = AcquireMutex(mbox->lock, TIMEOUT_INFINITE);
    if (res != 0) {
        ReleaseSem(mbox->empty_sem);
        return res;
    }

    memcpy(mbox->data + mbox->end_idx * mbox->message_size, msg, mbox->message_size);
    mbox->end_idx = (mbox->end_idx + 1) % mbox->max_count;
    mbox->count++;
    ReleaseMutex(mbox->lock);
    ReleaseSem(mbox->filled_sem);
    return 0;
}

export int KeGetMessageFromMany(struct msgbox** mboxes, int count, void* msg, int64_t timeout, bool remove, int* box_out) {
    if (msg == NULL || mboxes == NULL) {
        return EINVAL;
    }

    struct sem** sems = AllocHeap(count * sizeof(struct sem*));
    for (int i = 0; i < count; ++i) {
        sems[i] = mboxes[i]->filled_sem;
    }
    int selected_index;
    int res = AcquireSemFromMany(sems, count, timeout, &selected_index);
    if (res != 0) {
        return res;
    }
    if (box_out != NULL) {
        *box_out = selected_index;
    }
    LogPrintf("KeGetMessageFromMany: %d\n", selected_index);
    struct msgbox* mbox = mboxes[selected_index];
    res = AcquireMutex(mbox->lock, TIMEOUT_INFINITE);
    if (res != 0) {
        ReleaseSem(mbox->filled_sem);
        return res;
    }
    memcpy(msg, mbox->data + mbox->start_idx * mbox->message_size, mbox->message_size);
    if (remove) {
        mbox->start_idx = (mbox->start_idx + 1) % mbox->max_count;
        mbox->count--;
    }
    ReleaseMutex(mbox->lock);
    if (remove) {
        ReleaseSem(mbox->empty_sem);
    } else {
        ReleaseSem(mbox->filled_sem);
    }
    return 0;
}

export int KeGetMessage(struct msgbox* mbox, void* msg, int64_t timeout) {
    return KeGetMessageFromMany(&mbox, 1, msg, timeout, true, NULL);
}

export int KePeekMessage(struct msgbox* mbox, void* msg, int64_t timeout) {
    return KeGetMessageFromMany(&mbox, 1, msg, timeout, false, NULL);
}
