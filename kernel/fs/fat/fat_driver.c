
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

struct fat_request {
    bool is_directory;
    uint32_t cluster;       /* The file's or directory's first cluster. */
    uint32_t file_size;     /* Files only. */
    struct aio* aio;
};

static void FatDriverThread(void* _fat) {
    BeginNewThread();

    struct fat* fat = _fat;

    while (true) {
        struct fat_request req;
        if (KeGetMessage(fat->msgbox, &req, TIMEOUT_INFINITE) != 0) {
            continue;
        }

        int status;
        if (req.is_directory) {
            status = fat_read_directory_entry_sync(fat, req.cluster, &req.aio->transfer);
        } else {
            status = fat_read_file_sync(fat, req.cluster, req.file_size, &req.aio->transfer);
        }
        SignalDoneIO(req.aio, status);
    }
}

int fat_start_driver(struct fat* fat) {
    fat->table_lock = CreateMutex();
    if (fat->table_lock == NULL) {
        return ENOMEM;
    }

    fat->msgbox = CreateMessageBox(sizeof(struct fat_request), FAT_QUEUE_DEPTH);
    if (fat->msgbox == NULL) {
        DerefObject(fat->table_lock);
        return ENOMEM;
    }

    CreateThreadEx(GetKernelVas(), FatDriverThread, fat, 4096);
    return 0;
}

int fat_read_file(struct fat* fat, uint32_t first_cluster, uint32_t file_size, struct aio* aio) {
    struct fat_request req = {
        .is_directory = false,
        .cluster = first_cluster,
        .file_size = file_size,
        .aio = aio,
    };

    return KePostMessage(fat->msgbox, (const void*) &req, TIMEOUT_INSTANT);
}

int fat_read_directory_entry(struct fat* fat, uint32_t dir_cluster, struct aio* aio) {
    struct fat_request req = {
        .is_directory = true,
        .cluster = dir_cluster,
        .file_size = 0,
        .aio = aio,
    };

    return KePostMessage(fat->msgbox, (const void*) &req, TIMEOUT_INSTANT);
}
