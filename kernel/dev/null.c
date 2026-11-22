
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
#include <aio.h>

static pageable int ReadWrite(struct aio* aio) {
    struct transfer* io = &aio->transfer;
    io->offset = 0;
    io->address = ((uint8_t*) io->address) + io->length_remaining;
    io->length_remaining = 0;
    return 0;
}

static const struct vnode_operations dev_ops = {
    .read           = ReadWrite,
    .write          = ReadWrite,
};

void KeInitNullDevice(void) {
    struct vnode* node = KeCreateVnode(dev_ops, (struct stat) {
        .st_mode = S_IFCHR | S_IRWXU | S_IRWXG | S_IRWXO,
        .st_nlink = 1,
        .st_dev = KeNextDevId()
    });
    KeMount(node, "null");
    KeDerefObject(node);      // let the mount keep the last reference
}
