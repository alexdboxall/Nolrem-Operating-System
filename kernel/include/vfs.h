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

// TODO: how do we remove vnodes from the table? race between removing from table
// after ref count = 0 and someone looking up a vnode in the table
// or actually could make the table keep a ref... and have it keep recently closed
// and then have bg thread cleanup thread could sometimes lock the tree and clear out
// refcount = 1 (i.e. only the table) nodes