#pragma once

#include <sys/types.h>
#include <common.h>
#include <vfs.h>
#include <transfer.h>

struct aio;
struct msgbox;

/*
* Only 512 byte sectors are supported (that's what virtually all FAT12/FAT16
* volumes use).
*/
#define FAT_SECTOR_SIZE 512

#define FAT_TYPE_12 12
#define FAT_TYPE_16 16

/*
* Returned (internally) when a cluster chain or a directory has no more entries.
* Never conflicts with an errno value as those are positive.
*/
#define FAT_END (-1)

/*
* The inode number of the root directory. Real inode numbers are never 1, as
* they are based on a cluster number + 1 (and cluster 0 is not a cluster).
*/
#define FAT_ROOT_INO 1

/*
* Everything in here is set up when the volume is mounted and never modified
* afterwards, so it can be used from any thread without locking.
*
* All sector numbers are in units of FAT_SECTOR_SIZE, relative to the start of
* the volume (i.e. the partition).
*/
struct fat {
    struct file* disk;
    int type;                           /* FAT_TYPE_12 or FAT_TYPE_16 */
    uint32_t sectors_per_cluster;
    uint32_t cluster_bytes;
    uint32_t root_dir_start_sector;     /* FAT12/16 have a fixed size root directory outside the data area */
    uint32_t root_dir_sectors;
    uint32_t data_start_sector;         /* Cluster 2 starts here. */
    uint32_t max_cluster;               /* Highest valid cluster number. */
    uint8_t* table;                     /* An in-memory copy of the first FAT. */
    struct msgbox* msgbox;              /* Requests for the filesystem thread. */
};

/*
* What we know about a file or directory after finding its directory entry.
*
* A directory with first_cluster == 0 is the root directory (that's how ".."
* entries in subdirectories refer to it). A file with first_cluster == 0 is
* empty.
*/
struct fat_node_info {
    uint32_t first_cluster;
    uint32_t size;          /* Zero for directories. */
    bool directory;
    uint32_t ino;
};

int fat_mount(struct file* disk, struct fat** out);
void fat_destroy(struct fat* fat);
int fat_start_driver(struct fat* fat);

int fat_read_file(struct fat* fat, uint32_t first_cluster, uint32_t file_size, struct aio* aio);
int fat_read_directory_entry(struct fat* fat, uint32_t dir_cluster, struct aio* aio);
int fat_follow(struct fat* fat, uint32_t dir_cluster, const char* name, struct fat_node_info* out);
