#pragma once

/*
* Internal header for the FAT driver (FAT12, FAT16 and FAT32, read only, no long
* file names). The files that make up the driver are:
*
*   fat_core.c    synchronous disk access, mounting
*   fat_table.c   the File Allocation Table: the sector cache and cluster chains
*   fat_dir.c     directories: scanning, name matching, Follow, readdir
*   fat_file.c    reading file data
*   fat_driver.c  the filesystem thread and the queueing of requests
*   fat_vnode.c   the VFS glue
*/

#include <sys/types.h>
#include <common.h>
#include <vfs.h>
#include <transfer.h>
#include <endian.h>

struct aio;
struct msgbox;
struct mutex;

/*
* Only 512 byte sectors are supported (that's what virtually all FAT volumes
* use).
*/
#define FAT_SECTOR_SIZE 512

#define FAT_TYPE_12 12
#define FAT_TYPE_16 16
#define FAT_TYPE_32 32

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

#define FAT_ENTRY_SIZE         32
#define FAT_ENTRIES_PER_SECTOR (FAT_SECTOR_SIZE / FAT_ENTRY_SIZE)

/*
* A FAT directory has at most 65536 entries of 32 bytes. Anything beyond that
* means a corrupt (probably looping) cluster chain.
*/
#define FAT_MAX_DIR_SECTORS 4096

#define ATTR_VOLUME_ID  0x08
#define ATTR_DIRECTORY  0x10
#define ATTR_LFN_MASK   0x3F
#define ATTR_LFN        0x0F    /* Read only + hidden + system + volume ID. Marks a long file name entry. */

#define NT_LOWER_NAME   0x08    /* Windows NT: the name is lower case. */
#define NT_LOWER_EXT    0x10    /* Windows NT: the extension is lower case. */

/*
* Only a small part of the FAT is ever kept in memory: the two most recently
* used sectors of it. Two (rather than one) because a FAT12 entry is 12 bits
* wide, so one can start in one sector and end in the next.
*/
#define FAT_CACHE_SLOTS 2

struct fat_cache_slot {
    bool valid;
    uint32_t sector;                    /* Index of the sector within the FAT (not within the volume). */
    uint8_t data[FAT_SECTOR_SIZE];
};

/*
* Nearly everything in here is set up when the volume is mounted and never
* modified afterwards, so it can be used from any thread without locking. The
* exceptions are the sector cache (protected by table_lock) and the seek hint
* (only touched by the filesystem thread).
*
* All sector numbers are in units of FAT_SECTOR_SIZE, relative to the start of
* the volume (i.e. the partition).
*
* Directories and files are identified by the number of their first cluster. The
* exception is the FAT12/FAT16 root directory, which is not made of clusters but
* is a fixed area outside the data area: that is "directory cluster 0". On FAT32
* the root directory is an ordinary cluster chain starting at root_cluster.
* Either way, fat->root_cluster is the "cluster number" of the root directory.
*/
struct fat {
    struct file* disk;
    int type;                           /* FAT_TYPE_12, FAT_TYPE_16 or FAT_TYPE_32 */
    uint32_t sectors_per_cluster;
    uint32_t cluster_bytes;
    uint32_t root_cluster;              /* 0 on FAT12/FAT16 (see above), the real first cluster on FAT32. */
    uint32_t root_dir_start_sector;     /* FAT12/FAT16 only. */
    uint32_t root_dir_sectors;          /* FAT12/FAT16 only. */
    uint32_t data_start_sector;         /* Cluster 2 starts here. */
    uint32_t max_cluster;               /* Highest valid cluster number. */
    uint32_t fat_start_sector;          /* The copy of the FAT that we read from. */
    uint32_t fat_sectors;               /* How many sectors that copy has. */

    struct msgbox* msgbox;              /* Requests for the filesystem thread. */

    struct mutex* table_lock;           /* Held while looking at or changing the cache. */
    struct fat_cache_slot cache[FAT_CACHE_SLOTS];
    int cache_mru;                      /* Index of the most recently used slot. */

    /*
    * Remembers the last cluster of the last file that was read from, so a
    * sequence of reads of a file doesn't have to walk the chain from the start
    * each time. Only used by the filesystem thread.
    */
    uint32_t hint_first_cluster;        /* 0 = no hint. */
    uint32_t hint_index;                /* Which cluster of the file (0 = the first). */
    uint32_t hint_cluster;              /* The cluster number of that cluster. */
};

/*
* What we know about a file or directory after finding its directory entry.
*
* A file with first_cluster == 0 is empty. A directory's first_cluster is never
* 0 on FAT32. On FAT12/FAT16, 0 is the root directory.
*/
struct fat_node_info {
    uint32_t first_cluster;
    uint32_t size;          /* Zero for directories. */
    bool directory;
    uint32_t ino;
};

/* ---- Used by the rest of the filesystem (fat_vnode.c) ---- */

int fat_mount(struct file* disk, struct fat** out);
void fat_destroy(struct fat* fat);
int fat_start_driver(struct fat* fat);

int fat_read_file(struct fat* fat, uint32_t first_cluster, uint32_t file_size, struct aio* aio);
int fat_read_directory_entry(struct fat* fat, uint32_t dir_cluster, struct aio* aio);
int fat_follow(struct fat* fat, uint32_t dir_cluster, const char* name, struct fat_node_info* out);

/* ---- Shared between the files of the driver ---- */

/* fat_core.c */
int fat_disk_transfer(struct file* disk, struct transfer* io);
int fat_disk_read(struct file* disk, void* buffer, size_t length, uint64_t offset);

/* fat_table.c */
int fat_next_cluster(struct fat* fat, uint32_t cluster, uint32_t* next);
int fat_cluster_at(struct fat* fat, uint32_t first_cluster, uint32_t index, uint32_t* out);
void fat_set_hint(struct fat* fat, uint32_t first_cluster, uint32_t index, uint32_t cluster);
uint64_t fat_cluster_to_byte(const struct fat* fat, uint32_t cluster);

/* fat_dir.c */
int fat_read_directory_entry_sync(struct fat* fat, uint32_t dir_cluster, struct transfer* io);

/* fat_file.c */
int fat_read_file_sync(struct fat* fat, uint32_t first_cluster, uint32_t file_size, struct transfer* io);