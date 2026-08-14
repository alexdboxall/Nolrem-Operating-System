#pragma once

#include <common.h>
#include <sys/types.h>
#include <spinlock.h>
#include <obj.h>

struct vnode;

struct file {
    struct obj_header hdr;
    size_t seek_position;
    struct vnode* node;
    int flags : 30;
    int can_read : 1;
    int can_write : 1;
    mode_t initial_mode;
};

struct file* CreateFile(struct vnode* node, int mode, int flags, bool can_read, bool can_write);

void InitFile(void);