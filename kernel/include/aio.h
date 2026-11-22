#pragma once

#include <obj.h>
#include <mutex.h>
#include <common.h>
#include <transfer.h>
#include <vnode.h>
#include <stdatomic.h>

struct msgbox;

#define AIO_OP_READ         0
#define AIO_OP_WRITE        1
#define AIO_OP_TRUNCATE     2

struct aio {
    struct obj_header hdr;  // only needed for subIOs
    struct mutex mtx;   // starts acquired. disk releases when i/o is completed
    void (*callback)(void*);
    void* callback_ctxt;
    struct transfer transfer;
    int operation;
    union {
        struct {
            off_t size;
        } truncate;
    } args;
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


int KeCreateIO(struct aio* aio, void* node_or_file, struct transfer tr, void(*callback)(void*), void* callback_ctxt);
int KeCreateTruncateIO(struct aio* aio, void* node_or_file, off_t pos, void(*callback)(void*), void* callback_ctxt);

void KeSignalDoneIO(struct aio* aio, int result_code);
int KeWaitIO(struct aio* aio, uint64_t timeout);