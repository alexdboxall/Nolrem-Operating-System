
#include <common.h>
#include <vfs.h>
#include <errno.h>
#include <string.h>
#include <spinlock.h>
#include <diskutil.h>
#include <mutex.h>
#include <log.h>
#include <filesystem.h>
#include <heap.h>

//#include <fs/demofs/demofs.h>
#include <fs/fat/fat.h>

struct filesystem {
    fs_mount_creator mount_creator;
    struct filesystem* next;
    char name[];
};

static struct filesystem* registered_filesystems = NULL;
static struct mutex fs_table_lock;

void KeInitFilesystemTable(void) {
    KeInitStaticMutex(&fs_table_lock);
    FatInit();
}

export int KeRegisterFilesystem(char* fs_name, fs_mount_creator mount) {
    if (fs_name == NULL || mount == NULL) {
        return EINVAL;
    }

    /* Permanent memory can't be given back, so make sure we can get the lock before allocating. */
    int res = KeAcquireMutex(&fs_table_lock, TIMEOUT_DISK_OPERATION);
    if (res != 0) {
        return res;
    }

    struct filesystem* fs = KeAllocPermanentHeap(sizeof(struct filesystem) + strlen(fs_name) + 1);
    if (fs == NULL) {
        KeReleaseMutex(&fs_table_lock);
        return ENOMEM;
    }
    fs->mount_creator = mount;
    strcpy(fs->name, fs_name);

    fs->next = registered_filesystems;
    registered_filesystems = fs;
    KeReleaseMutex(&fs_table_lock);
  
    return 0;
}

export int KeMountFilesystemForDisk(struct file* partition) {
    /* The lock is held while each mount creator reads the disk to see if it recognises it. */
    int lock_res = KeAcquireMutex(&fs_table_lock, TIMEOUT_DISK_OPERATION);
    if (lock_res != 0) {
        return lock_res;
    }

    struct filesystem* fs = registered_filesystems;
    struct file* file_out = NULL;   /* stays NULL if nobody (or nothing) recognises the disk */
    while (fs) {
        file_out = NULL;
        int res = fs->mount_creator(partition, &file_out);
        if (res == 0) {
            break;
        }
        file_out = NULL;
        fs = fs->next;
    }
    KeReleaseMutex(&fs_table_lock);

    if (fs == NULL || file_out == NULL) {
        return ENODEV;
    }

    int res = VnodeCreate(partition->node, &file_out->node, "fs", 0, 0);
    if (res != 0) {
        return res;
    }

    return KeMount(file_out->node, KeGenerateNewMountedDiskName());
}