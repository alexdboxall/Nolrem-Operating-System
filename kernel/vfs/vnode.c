#include <common.h>
#include <vnode.h>
#include <heap.h>
#include <errno.h>
#include <log.h>
#include <dirent.h>
#include <sys/ioctl.h>

export struct vnode* CreateVnode(struct vnode_operations ops, struct stat st) {
    struct vnode* node = AllocHeap(sizeof(struct vnode));
    InitObject(node, OBJTYPE_VNODE);
    node->data = NULL;
    node->stat = st;
    node->flags = 0;
    node->ops = ops;
    return node;
}

static void CleanupVnode(void* _node) {
    struct vnode* node = _node;
    if (node->stat.st_nlink == 0 && node->ops.delete != NULL) {
        node->ops.delete(node);
    }
    if (node->ops.cleanup != NULL) {
        node->ops.cleanup(node);
    }
    FreeHeap(node);
}

void InitVnode(void) {
    RegisterObjectType(OBJTYPE_VNODE, CleanupVnode);
}

static void CheckVnode(struct vnode* node) {
    (void) node;
    return;
}

int VnodeCheckOpen(struct vnode* node, int flags) {
    CheckVnode(node);
    if (node->ops.check_open == NULL) {
        return 0;
    }
    return node->ops.check_open(node, flags);
}

int VnodeRead(struct vnode* node, struct transfer* io) {
    CheckVnode(node);
    if (node->ops.read == NULL || io->direction != TRANSFER_READ) {
        return EINVAL;
    }
    return node->ops.read(node, io);
}

int VnodeWrite(struct vnode* node, struct transfer* io) {
    CheckVnode(node);
    if (node->ops.write == NULL || io->direction != TRANSFER_WRITE) {
        return EINVAL;
    }
    return node->ops.write(node, io);
}

int VnodeIoctl(struct vnode* node, int command, void* buffer) {
    CheckVnode(node);
    if (node->ops.ioctl == NULL) {
        if (command == TCGETS || command == TCSETS || command == TCSETSW || command == TCSETSF ||
            command == TIOCGPGRP || command == TIOCSPGRP) {
            return ENOTTY;
        }
        return EINVAL;
    }
    return node->ops.ioctl(node, command, buffer);
}

int VnodeCreate(struct vnode* node, struct vnode** out, const char* name, int flags, mode_t mode) {
    CheckVnode(node);
    if (node->ops.create == NULL) {
        return EINVAL;
    }
    return node->ops.create(node, out, name, flags, mode);
}

int VnodeTruncate(struct vnode* node, off_t offset) {
    CheckVnode(node);
    if (node->ops.truncate == NULL) {
        return EINVAL;
    }
    return node->ops.truncate(node, offset);
}

int VnodeFollow(struct vnode* node, struct vnode** new_node, const char* name) {
    CheckVnode(node);
    if (node->ops.follow == NULL) {
        return ENOTDIR;
    }
    return node->ops.follow(node, new_node, name);
}

uint8_t VnodeDirentType(struct vnode* node) {
    return IFTODT(node->stat.st_mode);
}

int VnodeUnlink(struct vnode* node) {
    CheckVnode(node);
    if (node->ops.unlink == NULL) {
        return EINVAL;
    }
    return node->ops.unlink(node);
}

int VnodeDelete(struct vnode* node) {
    CheckVnode(node);
    if (node->ops.delete == NULL) {
        return EINVAL;
    }
    return node->ops.delete(node);
}