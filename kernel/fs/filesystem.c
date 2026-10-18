
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

void InitFilesystemTable(void) {
    InitStaticMutex(&fs_table_lock);
    InitFat();
}

export int RegisterFilesystem(char* fs_name, fs_mount_creator mount) {
    if (fs_name == NULL || mount == NULL) {
        return EINVAL;
    }

    struct filesystem* fs = AllocPermanentHeap(sizeof(struct filesystem) + strlen(fs_name) + 1);
    fs->mount_creator = mount;
    strcpy(fs->name, fs_name);

    AcquireMutex(&fs_table_lock, TIMEOUT_INFINITE);
    fs->next = registered_filesystems;
    registered_filesystems = fs;
    ReleaseMutex(&fs_table_lock);
  
    return 0;
}

export int MountFilesystemForDisk(struct file* partition) {
    AcquireMutex(&fs_table_lock, TIMEOUT_INFINITE);

    struct filesystem* fs = registered_filesystems;
    struct file* file_out;
    while (fs) {
        int res = fs->mount_creator(partition, &file_out);
        if (res == 0) {
            break;
        }
        fs = fs->next;
    }
    ReleaseMutex(&fs_table_lock);

    if (file_out == NULL) {
        return ENODEV;
    }

    int res = VnodeCreate(partition->node, &file_out->node, "fs", 0, 0);
    if (res != 0) {
        return res;
    }

    return Mount(file_out->node, GenerateNewMountedDiskName());
}