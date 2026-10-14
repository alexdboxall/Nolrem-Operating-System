#pragma once

#include <obj.h>
#include <mutex.h>
#include <common.h>
#include <transfer.h>
#include <vnode.h>
#include <stdatomic.h>

struct msgbox;

struct aio {
    struct obj_header hdr;  // only needed for subIOs
    struct mutex mtx;   // starts acquired. disk releases when i/o is completed
    void (*callback)(void*);
    void* callback_ctxt;
    struct transfer transfer;
    union {
        struct vnode* node;
        struct file* file;
        void* node_or_file;
    };
    struct aio* sub;
    int result_code;
    size_t extra_data;
    _Atomic bool completed;
};


int CreateIO(struct aio* aio, void* node_or_file, struct transfer tr, void(*callback)(void*), void* callback_ctxt);
void SignalDoneIO(struct aio* aio, int result_code);
int WaitIO(struct aio* aio, uint64_t timeout);