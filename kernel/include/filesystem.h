#pragma once

#include <common.h>

struct file;

typedef int(*fs_mount_creator)(struct file*, struct file**);

void KeInitFilesystemTable(void);
int KeRegisterFilesystem(char* fs_name, fs_mount_creator mount);
int KeMountFilesystemForDisk(struct file* partition);