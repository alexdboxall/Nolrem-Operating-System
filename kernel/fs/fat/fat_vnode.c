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
    bool directory;

    /*
    * The first cluster (the root directory's is fat->root_cluster, 0 for empty
    * files) and the length. These change when the file is written to or truncated
    * (see struct fat_file).
    */
    struct fat_file file;
};

static int CheckOpen(struct vnode* node, int flags) {
    struct vnode_data* data = node->data;

    if (data->directory && ((flags & O_ACCMODE) == O_WRONLY || (flags & O_ACCMODE) == O_RDWR)) {
        return EISDIR;
    }

    return 0;
}

static int Ioctl(struct vnode*, int, void*) {
    return EINVAL;
}

static int Read(struct aio* aio) { 
    assert(aio->operation == AIO_OP_READ);

    struct vnode* node = aio->node;   
    struct vnode_data* data = node->data;
    if (data->directory) {
        return FatReadDirectoryEntry(data->fs, data->file.first_cluster, aio);
    } else {
        return FatReadFile(data->fs, &data->file, aio);
    }
}

static int Write(struct aio* aio) {
    assert(aio->operation == AIO_OP_WRITE);

    struct vnode* node = aio->node;
    struct vnode_data* data = node->data;
    if (data->directory) {
        return EISDIR;
    }

    return FatWriteFile(data->fs, node, &data->file, aio);
}

static int Truncate(struct aio* aio) {
    assert(aio->operation == AIO_OP_TRUNCATE);

    struct vnode* node = aio->node;
    struct vnode_data* data = node->data;
    if (data->directory) {
        return EISDIR;
    }

    return FatTruncateFile(data->fs, node, &data->file, aio);
}

static struct vnode* CreateFatVnode(uint32_t ino, bool directory, uint32_t size, dev_t dev);

/*
* Makes the vnode for something that was found in (or added to) a directory.
*/
static int CreateChildVnode(struct vnode* parent, const struct fat_node_info* info, struct vnode** out) {
    struct vnode_data* parent_data = parent->data;

    struct vnode_data* child_data = AllocHeap(sizeof(struct vnode_data));
    if (child_data == NULL) {
        return ENOMEM;
    }

    child_data->fs = parent_data->fs;
    child_data->directory = info->directory;
    child_data->file.first_cluster = info->first_cluster;
    child_data->file.size = info->size;
    child_data->file.entry_byte = info->entry_byte;

    struct vnode* child_node = CreateFatVnode(info->ino, info->directory, info->size, parent->stat.st_dev);
    if (child_node == NULL) {
        FreeHeap(child_data);
        return ENOMEM;
    }

    child_node->data = child_data;

    *out = child_node;
    return RegisterNewVnode(child_node);
}

/*
* Creates an empty file. Only names that fit in "8.3" can be used (EINVAL
* otherwise).
*/
static int Create(struct vnode* node, struct vnode** out, const char* name, int, mode_t) {
    struct vnode_data* data = node->data;
    if (!data->directory) {
        return ENOTDIR;
    }

    struct fat_node_info info;
    int status = FatCreate(data->fs, data->file.first_cluster, name, &info);
    if (status != 0) {
        return status;
    }

    return CreateChildVnode(node, &info, out);
}

static int Follow(struct vnode* node, struct vnode** out, const char* name) {
    struct vnode_data* data = node->data;
    if (!data->directory) {
        return ENOTDIR;
    }

    struct fat_node_info info;
    int status = FatFollow(data->fs, data->file.first_cluster, name, &info);
    if (status != 0) {
        return status;
    }

    /*
    * If it's already open then share its vnode, as that's where the up to date
    * size lives.
    */
    struct vnode* existing = TryFindAndReferenceExistingVnode(info.ino, node->stat.st_dev);
    if (existing) {
        *out = existing;
        return 0;
    }

    return CreateChildVnode(node, &info, out);
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
    * FAT has no permissions, so everything is accessible to everyone.
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
    int status = FatMount(raw_device, &fat);
    if (status != 0) {
        return status;
    }

    struct vnode_data* data = AllocHeap(sizeof(struct vnode_data));
    if (data == NULL) {
        FatDestroy(fat);
        return ENOMEM;
    }

    data->fs = fat;
    data->directory = true;
    data->file.first_cluster = fat->root_cluster;   /* root directory (0 on FAT12/FAT16, as it isn't in a cluster) */
    data->file.size = 0;            /* root directory has no length */
    data->file.entry_byte = 0;      /* ... or an entry */

    /*
    * Last, as the thread can't be stopped again.
    */
    status = FatStartDriver(fat);
    if (status != 0) {
        FreeHeap(data);
        FatDestroy(fat);
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
