#pragma once

#include <common.h>
#include <sys/types.h>
#include <file.h>
#include <vnode.h>
#include <transfer.h>

void KeInitVfs(void);
//void InitRootsFilesystem(void);

struct aio;

int KeMount(struct vnode* node, const char* name);
int KeUnmount(const char* name);

int KeOpenFile(const char* path, int flags, mode_t mode, struct file** out);
int KeReadFile(struct aio* aio);
int KeWriteFile(struct aio* aio);
int KeRemoveFileOrDirectory(const char* path, bool rmdir);

int KeSetWorkingDirectory(struct vnode* node);

int KeNextDevId(void);

struct vnode* KeTryFindAndReferenceExistingVnode(ino_t id, dev_t dev);
int KeRegisterNewVnode(struct vnode* node);
void KePurgeOldVnodes(void);

/*
 * The table holds its own reference to every registered vnode, so a lookup can
 * never race with the last user dropping it. Vnodes that nobody else holds
 * (refcount == 1) are evicted by KePurgeOldVnodes(), which runs from the
 * low-priority background thread; KeUnregisterVnode() evicts one explicitly.
 */