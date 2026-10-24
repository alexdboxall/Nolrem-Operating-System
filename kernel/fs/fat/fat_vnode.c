#include <heap.h>
#include <log.h>
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <vfs.h>
#include <transfer.h>
#include <string.h>
#include <dirent.h>
#include <aio.h>
#include <filesystem.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <fs/fat/fat_private.h>

struct vnode_data {
    struct fat* fs;             /* Shared by every vnode on the volume. */
    uint32_t first_cluster;     /* The root directory's is fat->root_cluster. 0 for empty files. */
    uint32_t file_length;
    bool directory;
};

static int CheckOpen(struct vnode*, int flags) {
    if ((flags & O_ACCMODE) == O_WRONLY || (flags & O_ACCMODE) == O_RDWR) {
        return EROFS;
    }

    return 0;
}

static int Ioctl(struct vnode*, int, void*) {
    return EINVAL;
}

static int Read(struct aio* aio) { 
    struct vnode* node = aio->node;   
    struct vnode_data* data = node->data;
    if (data->directory) {
        return fat_read_directory_entry(data->fs, data->first_cluster, aio);
    } else {
        return fat_read_file(data->fs, data->first_cluster, data->file_length, aio);
    }
}

static int Write(struct aio*) {
    return EROFS;
}

static int Create(struct vnode*, struct vnode**, const char*, int, mode_t) {
    return EROFS;
}

static int Truncate(struct vnode*, off_t) {
    return EROFS;
}

static struct vnode* CreateFatVnode(uint32_t ino, bool directory, uint32_t size, dev_t dev);

static int Follow(struct vnode* node, struct vnode** out, const char* name) {
    struct vnode_data* data = node->data;
    if (!data->directory) {
        return ENOTDIR;
    }

    struct fat_node_info info;
    int status = fat_follow(data->fs, data->first_cluster, name, &info);
    if (status != 0) {
        return status;
    }

    /*
    * TODO: return existing vnode if someone opens the same file twice...
    */
    struct vnode* existing = TryFindAndReferenceExistingVnode(info.ino, node->stat.st_dev);
    if (existing) {
        *out = existing;
        return 0;
    }

    struct vnode_data* child_data = AllocHeap(sizeof(struct vnode_data));
    if (child_data == NULL) {
        return ENOMEM;
    }

    child_data->fs = data->fs;
    child_data->first_cluster = info.first_cluster;
    child_data->file_length = info.size;
    child_data->directory = info.directory;

    struct vnode* child_node = CreateFatVnode(info.ino, info.directory, info.size, node->stat.st_dev);
    child_node->data = child_data;

    *out = child_node;
    return RegisterNewVnode(child_node);
}

static const struct vnode_operations dev_ops = {
    .check_open     = CheckOpen,
    .ioctl          = Ioctl,
    .read           = Read,
    .write          = Write,
    .truncate       = Truncate,
    .create         = Create,
    .follow         = Follow,
};

static struct vnode* CreateFatVnode(uint32_t ino, bool directory, uint32_t size, dev_t dev) {
    /*
    * FAT has no permissions, so everything is accessible to everyone. Writing is
    * stopped by CheckOpen anyway.
    */
    return CreateVnode(dev_ops, (struct stat) {
        .st_mode = (directory ? S_IFDIR : S_IFREG) | S_IRWXU | S_IRWXG | S_IRWXO,
        .st_nlink = 1,
        .st_size = size,
        .st_ino = ino,
        .st_blksize = 512,      // the 'efficient' size
        .st_dev = dev
    });
}

int FatMountCreator(struct file* raw_device, struct file** out) {
    struct fat* fat;
    int status = fat_mount(raw_device, &fat);
    if (status != 0) {
        return status;
    }

    struct vnode_data* data = AllocHeap(sizeof(struct vnode_data));
    if (data == NULL) {
        fat_destroy(fat);
        return ENOMEM;
    }

    data->fs = fat;
    data->first_cluster = fat->root_cluster;    /* root directory (0 on FAT12/FAT16, as it isn't in a cluster) */
    data->file_length = 0;          /* root directory has no length */
    data->directory = true;

    /*
    * Last, as the thread can't be stopped again.
    */
    status = fat_start_driver(fat);
    if (status != 0) {
        FreeHeap(data);
        fat_destroy(fat);
        return status;
    }

    struct vnode* node = CreateFatVnode(FAT_ROOT_INO, true, 0, raw_device->node->stat.st_dev);
    node->data = data;

    *out = CreateFile(node, 0, 0, true, false);
    return 0;
}

void InitFat(void) {
    RegisterFilesystem("fat", FatMountCreator);
}
