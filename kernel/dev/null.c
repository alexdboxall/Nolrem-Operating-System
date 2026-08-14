
/*
 * dev/null.c - Null Device
 *
 * A device which ignores any read or write operations.
 */

#include <heap.h>
#include <vfs.h>
#include <log.h>
#include <errno.h>
#include <transfer.h>
#include <sys/stat.h>
#include <dirent.h>

static pageable int ReadWrite(struct vnode*, struct transfer* io) {
    io->offset = 0;
    io->address = ((uint8_t*) io->address) + io->length_remaining;
    io->length_remaining = 0;
    return 0;
}

static const struct vnode_operations dev_ops = {
    .read           = ReadWrite,
    .write          = ReadWrite,
};

void InitNullDevice(void)
{
    struct vnode* node = CreateVnode(dev_ops, (struct stat) {
        .st_mode = S_IFCHR | S_IRWXU | S_IRWXG | S_IRWXO,
        .st_nlink = 1,
        .st_dev = NextDevId()
    });
    Mount(node, "null");
    DerefObject(node);      // let the mount keep the last reference
}
