#pragma once

/*
* Internal header for the FAT driver (FAT12, FAT16 and FAT32, no long file
* names). Files can be read, written (including growing them), created and
* truncated. Deleting, renaming and making directories are not supported. The
* files that make up the driver are:
*
*   fat_core.c    synchronous disk access, mounting
*   fat_table.c   the File Allocation Table: the sector cache, cluster chains
*                 and cluster allocation
*   fat_dir.c     directories: scanning, name matching, Follow, readdir, Create
*   fat_file.c    reading, writing and truncating file data
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
#define ATTR_ARCHIVE    0x20    /* "Changed since the last backup". Set on new and modified files, like everyone else does. */
#define ATTR_LFN_MASK   0x3F
#define ATTR_LFN        0x0F    /* Read only + hidden + system + volume ID. Marks a long file name entry. */

#define NT_LOWER_NAME   0x08    /* Windows NT: the name is lower case. */
#define NT_LOWER_EXT    0x10    /* Windows NT: the extension is lower case. */

/*
* The size of a file is stored in 32 bits, so this is the most a file can hold.
*/
#define FAT_MAX_FILE_SIZE 0xFFFFFFFFu

/*
* Directory entries have a date and time on them. Until the kernel has a clock
* to take them from, new files are stamped with 1 January 1980, midnight (the
* earliest date that FAT can store).
*/
#define FAT_DEFAULT_DATE 0x0021
#define FAT_DEFAULT_TIME 0x0000

/* Store a little endian value (the counterparts of Le16 and Le32). */
static inline void FatPut16(uint8_t* p, uint32_t v) {
    p[0] = (uint8_t) v;
    p[1] = (uint8_t) (v >> 8);
}

static inline void FatPut32(uint8_t* p, uint32_t v) {
    p[0] = (uint8_t) v;
    p[1] = (uint8_t) (v >> 8);
    p[2] = (uint8_t) (v >> 16);
    p[3] = (uint8_t) (v >> 24);
}

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
* exceptions are:
*
*   - the sector cache (protected by table_lock),
*   - the seek hint (only touched by the filesystem thread),
*   - the allocation state (alloc_hint and fsinfo_done, protected by write_lock).
*
* Anything that changes the volume (writing to a file, creating one) holds
* write_lock for the whole operation. That keeps two cluster allocations from
* picking the same cluster, and stops two read-modify-writes of the same
* directory sector from losing each other's changes. Reads (including looking
* names up in a directory) don't need it. The order of locking is always
* write_lock and then table_lock.
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
    uint32_t fat_count;                 /* How many copies of the FAT there are. */

    /*
    * Normally every copy of the FAT is kept the same, so a write has to go to
    * all of them (fat_start_sector is then the first copy). FAT32 can instead
    * say that only one copy is in use, in which case that's the only one to write.
    */
    bool fat_mirrored;

    uint32_t fsinfo_sector;             /* FAT32 only: the FSInfo sector, 0 if there isn't a valid looking one. */
    bool fsinfo_done;                   /* The FSInfo sector has been marked as out of date (see FatAllocCluster). */
    uint32_t alloc_hint;                /* Where to start looking for a free cluster. */

    struct msgbox* msgbox;              /* Requests for the filesystem thread. */

    struct mutex* write_lock;           /* Held for the whole of anything that changes the volume. */
    struct mutex* table_lock;           /* Held while looking at or changing the cache. */
    struct fat_cache_slot cache[FAT_CACHE_SLOTS];
    int cache_mru;                      /* Index of the most recently used slot. */

    /*
    * Remembers the last cluster of the last file that was looked at, so a
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
    uint64_t entry_byte;    /* Where the directory entry is, in bytes from the start of the volume. */
};

/*
* The parts of a file that change when it is written to or truncated. This lives
* in the file's vnode. It is only changed by the filesystem thread, with
* write_lock held, so the filesystem thread can read it without locking.
*
* For a directory, first_cluster is the directory's cluster (see above) and the
* rest is unused.
*/
struct fat_file {
    uint32_t first_cluster;     /* 0 for a file that has no data yet. */
    uint32_t size;
    uint64_t entry_byte;        /* Where our directory entry is (see fat_node_info). It has to be updated when the above change. */
};

/* ---- Used by the rest of the filesystem (fat_vnode.c) ---- */

int FatMount(struct file* disk, struct fat** out);
void FatDestroy(struct fat* fat);
int FatStartDriver(struct fat* fat);

int FatReadFile(struct fat* fat, struct fat_file* file, struct aio* aio);
int FatWriteFile(struct fat* fat, struct vnode* node, struct fat_file* file, struct aio* aio);
int FatTruncateFile(struct fat* fat, struct vnode* node, struct fat_file* file, struct aio* aio);
int FatReadDirectoryEntry(struct fat* fat, uint32_t dir_cluster, struct aio* aio);
int FatFollow(struct fat* fat, uint32_t dir_cluster, const char* name, struct fat_node_info* out);
int FatCreate(struct fat* fat, uint32_t dir_cluster, const char* name, struct fat_node_info* out);

/* ---- Shared between the files of the driver ---- */

/* fat_core.c */
int FatDiskTransfer(struct file* disk, struct transfer* io);
int FatDiskWriteTransfer(struct file* disk, struct transfer* io);
int FatDiskRead(struct file* disk, void* buffer, size_t length, uint64_t offset);
int FatDiskWrite(struct file* disk, const void* buffer, size_t length, uint64_t offset);
int FatDiskZero(struct file* disk, uint64_t offset, uint64_t length);

/* fat_table.c */
int FatNextCluster(struct fat* fat, uint32_t cluster, uint32_t* next);
int FatClusterAt(struct fat* fat, uint32_t first_cluster, uint32_t index, uint32_t* out);
void FatSetHint(struct fat* fat, uint32_t first_cluster, uint32_t index, uint32_t cluster);
uint64_t FatClusterToByte(const struct fat* fat, uint32_t cluster);

/* fat_table.c, for changing the FAT. Only with write_lock held. */
int FatAllocCluster(struct fat* fat, uint32_t near, uint32_t* out);
int FatSetNext(struct fat* fat, uint32_t cluster, uint32_t next);
void FatFreeCluster(struct fat* fat, uint32_t cluster);
int FatAppendCluster(struct fat* fat, uint32_t last, uint32_t* out);
int FatClusterAtExtend(struct fat* fat, uint32_t* first_cluster, uint32_t index, uint32_t* out);
int FatTrimChain(struct fat* fat, uint32_t* first_cluster, uint32_t keep);

/* fat_dir.c */
int FatReadDirectoryEntrySync(struct fat* fat, uint32_t dir_cluster, struct transfer* io);
int FatUpdateEntry(struct fat* fat, const struct fat_file* file);    /* With write_lock held. */

/* fat_file.c */
int FatReadFileSync(struct fat* fat, struct fat_file* file, struct transfer* io);
int FatWriteFileSync(struct fat* fat, struct fat_file* file, struct transfer* io);
int FatTruncateFileSync(struct fat* fat, struct fat_file* file, uint64_t new_size);