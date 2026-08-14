#pragma once

#include <common.h>
#include <sys/types.h>
#include <file.h>
#include <vnode.h>
#include <transfer.h>

void InitVfs(void);
//void InitRootsFilesystem(void);

int Mount(struct vnode* node, const char* name);
int Unmount(const char* name);

int OpenFile(const char* path, int flags, mode_t mode, struct file** out);
int ReadFile(struct file* file, struct transfer* io);
int WriteFile(struct file* file, struct transfer* io);
int RemoveFileOrDirectory(const char* path, bool rmdir);

int SetWorkingDirectory(struct vnode* node);

int NextDevId(void);