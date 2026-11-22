
#include <common.h>
#include <errno.h>
#include <vfs.h>
#include <string.h>
#include <assert.h>
#include <transfer.h>
#include <log.h>
#include <heap.h>
#include <aio.h>
#include <thread.h>
#include <vmm.h>
#include <scheduler.h>
#include <msgbox.h>
#include <mutex.h>
#include <fs/fat/fat_private.h>

#define FAT_QUEUE_DEPTH 16

/*
* What the request is for is in aio->operation. (So that there's one place that
* says what the kinds of request are.)
*
* Files are identified by the vnode's fat_file rather than by copies of its
* values, as an earlier request might change them before this one runs.
*
* The aio keeps the vnode (and so the fat_file) alive until it's signalled.
*/
struct fat_request {
    struct aio* aio;
    bool directory;
    uint32_t cluster;       /* Directories: the first cluster. */
    struct fat_file* file;  /* Files. */
    struct vnode* node;     /* Files: whose st_size to keep up to date. */
};

static void FatDriverThread(void* _fat) {
    KeBeginNewThread();

    struct fat* fat = _fat;

    while (true) {
        struct fat_request req;
        if (KeGetMessage(fat->msgbox, &req, TIMEOUT_INFINITE) != 0) {
            continue;
        }

        int status;
        if (req.directory) {
            /* Directories can only be read, and the vnode doesn't let anything else get this far. */
            status = FatReadDirectoryEntrySync(fat, req.cluster, &req.aio->transfer);
        } else {
            switch (req.aio->operation) {
            case AIO_OP_READ:
                status = FatReadFileSync(fat, req.file, &req.aio->transfer);
                break;

            case AIO_OP_WRITE:
                status = FatWriteFileSync(fat, req.file, &req.aio->transfer);

                /* Even if it failed, some of it might have been written. */
                req.node->stat.st_size = req.file->size;
                break;

            case AIO_OP_TRUNCATE:
                status = FatTruncateFileSync(fat, req.file, (uint64_t) req.aio->args.truncate.size);
                req.node->stat.st_size = req.file->size;
                break;

            default:
                status = EINVAL;
                break;
            }
        }
        KeSignalDoneIO(req.aio, status);
    }
}

int FatStartDriver(struct fat* fat) {
    fat->table_lock = KeCreateMutex();
    if (fat->table_lock == NULL) {
        return ENOMEM;
    }

    fat->write_lock = KeCreateMutex();
    if (fat->write_lock == NULL) {
        KeDerefObject(fat->table_lock);
        return ENOMEM;
    }

    fat->msgbox = KeCreateMessageBox(sizeof(struct fat_request), FAT_QUEUE_DEPTH);
    if (fat->msgbox == NULL) {
        KeDerefObject(fat->write_lock);
        KeDerefObject(fat->table_lock);
        return ENOMEM;
    }

    KeCreateThreadEx(KeGetKernelVas(), FatDriverThread, fat, 4096);
    return 0;
}

/*
* These queue the work for the filesystem thread, which signals the aio when
* it's done. A non-zero return means it couldn't be queued (and so it will never
* be signalled).
*/
static int FatPostFileRequest(struct fat* fat, struct vnode* node, struct fat_file* file, struct aio* aio) {
    struct fat_request req = {
        .aio = aio,
        .directory = false,
        .cluster = 0,
        .file = file,
        .node = node,
    };

    return KePostMessage(fat->msgbox, (const void*) &req, TIMEOUT_INSTANT);
}

int FatReadFile(struct fat* fat, struct fat_file* file, struct aio* aio) {
    return FatPostFileRequest(fat, NULL, file, aio);
}

int FatWriteFile(struct fat* fat, struct vnode* node, struct fat_file* file, struct aio* aio) {
    return FatPostFileRequest(fat, node, file, aio);
}

int FatTruncateFile(struct fat* fat, struct vnode* node, struct fat_file* file, struct aio* aio) {
    return FatPostFileRequest(fat, node, file, aio);
}

int FatReadDirectoryEntry(struct fat* fat, uint32_t dir_cluster, struct aio* aio) {
    struct fat_request req = {
        .aio = aio,
        .directory = true,
        .cluster = dir_cluster,
        .file = NULL,
        .node = NULL,
    };

    return KePostMessage(fat->msgbox, (const void*) &req, TIMEOUT_INSTANT);
}
