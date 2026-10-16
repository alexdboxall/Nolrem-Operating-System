#pragma once

#include <sys/types.h>
#include <common.h>
#include <vfs.h>
#include <transfer.h>

struct msgbox;

struct demofs {
    struct file* disk;
    ino_t root_inode;  
    struct msgbox* msgbox;
};

struct demofs_request {
    bool is_directory;
    ino_t inode;            /* The file's first sector, or the directory's inode. */
    uint32_t file_size;     /* Files only. */
    struct aio* aio;
};

#define MAX_NAME_LENGTH 24

#define INODE_TO_SECTOR(inode) (inode & 0xFFFFFF)
#define INODE_IS_DIR(inode) (inode >> 31)
#define INODE_TO_DIR(inode) (inode | (1U << 31U))

int demofs_read_file(struct demofs* fs, ino_t file, uint32_t file_size, struct aio* aio);
int demofs_read_directory_entry(struct demofs* fs, ino_t directory, struct aio* aio);
int demofs_follow(struct demofs* fs, ino_t parent, ino_t* child, const char* name, uint32_t* file_length_out);

int demofs_start_driver(struct demofs*);