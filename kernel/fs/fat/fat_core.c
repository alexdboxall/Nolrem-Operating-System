/*
* fat_core.c - FAT12/FAT16/FAT32 (read only)
*
* Synchronous access to the underlying disk, and mounting.
*
* No long file name support: long name entries are skipped, so files are only
* ever seen by their 8.3 short name.
*/

#include <common.h>
#include <errno.h>
#include <vfs.h>
#include <string.h>
#include <assert.h>
#include <transfer.h>
#include <log.h>
#include <heap.h>
#include <aio.h>
#include <fs/fat/fat_private.h>

/*
* Tear down an aio that was created with CreateIO but which the driver failed to
* start (so it will never be signalled). Signalling it ourselves lets WaitIO
* drop the reference to the file and destroy the mutex.
*/
static void fat_io_abort(struct aio* aio, int status) {
    aio->callback = NULL;
    aio->sub = NULL;
    SignalDoneIO(aio, status);
    WaitIO(aio, TIMEOUT_INFINITE);
}

/*
* Synchronous read from the disk.
*/
int fat_disk_transfer(struct file* disk, struct transfer* io) {
    struct aio aio;
    CreateIO(&aio, disk, *io, NULL, NULL);

    int status = ReadFile(&aio);
    if (status != 0) {
        fat_io_abort(&aio, status);
        return status;
    }

    status = WaitIO(&aio, TIMEOUT_INFINITE);
    if (status == 0) {
        *io = aio.transfer;
    }

    return status;
}

/*
* Synchronous read of exactly 'length' bytes from the disk into a kernel buffer.
* The offset and length must be multiples of the sector size.
*/
int fat_disk_read(struct file* disk, void* buffer, size_t length, uint64_t offset) {
    struct transfer io = CreateKernelTransfer(buffer, length, offset, TRANSFER_READ);

    int status = fat_disk_transfer(disk, &io);
    if (status == 0 && io.length_remaining != 0) {
        /* A short read means something is wrong with the disk. */
        status = EIO;
    }

    return status;
}


/*
* The largest cluster number whose FAT entry lies entirely inside a FAT that is
* 'fat_bytes' long.
*/
static uint64_t fat_table_capacity(int type, uint64_t fat_bytes) {
    switch (type) {
    case FAT_TYPE_16:
        return fat_bytes / 2 - 1;

    case FAT_TYPE_32:
        return fat_bytes / 4 - 1;

    default: {
        /*
        * FAT12: entry N is at byte N + N/2 and needs that byte and the next one.
        */
        uint64_t n = (fat_bytes - 2) * 2 / 3;
        while (n + 1 + (n + 1) / 2 + 2 <= fat_bytes) {
            ++n;
        }
        while (n + n / 2 + 2 > fat_bytes) {
            --n;
        }
        return n;
    }
    }
}

/*
* Checks that the disk holds a FAT12, FAT16 or FAT32 volume. Returns ENOTSUP if
* it doesn't. Only the boot sector is read, nothing of the FAT is loaded here.
*/
int fat_mount(struct file* disk, struct fat** out) {
    uint32_t block_size = disk->node->stat.st_blksize;
    if (block_size == 0 || FAT_SECTOR_SIZE % block_size != 0) {
        return ENOTSUP;
    }

    uint8_t boot[FAT_SECTOR_SIZE];
    if (fat_disk_read(disk, boot, FAT_SECTOR_SIZE, 0) != 0) {
        return ENOTSUP;
    }

    /*
    * Cheap sanity checks first, so we reject other filesystems quickly.
    */
    if (boot[510] != 0x55 || boot[511] != 0xAA) {
        return ENOTSUP;
    }
    if (boot[0] != 0xEB && boot[0] != 0xE9) {
        return ENOTSUP;
    }

    uint32_t bytes_per_sector = Le16(boot + 11);
    uint32_t sectors_per_cluster = boot[13];
    uint32_t reserved_sectors = Le16(boot + 14);
    uint32_t fat_count = boot[16];
    uint32_t root_entries = Le16(boot + 17);
    uint32_t total_sectors = Le16(boot + 19);
    uint8_t media = boot[21];
    uint32_t fat_sectors = Le16(boot + 22);

    if (total_sectors == 0) {
        total_sectors = Le32(boot + 32);
    }

    if (media != 0xF0 && media < 0xF8) {
        return ENOTSUP;
    }

    if (bytes_per_sector != FAT_SECTOR_SIZE) {
        return ENOTSUP;
    }

    if (sectors_per_cluster == 0 || (sectors_per_cluster & (sectors_per_cluster - 1)) != 0) {
        return ENOTSUP;
    }

    bool is_fat32 = (fat_sectors == 0);
    uint32_t root_cluster = 0;
    uint32_t root_dir_sectors = 0;
    uint32_t active_fat = 0;

    if (is_fat32) {
        fat_sectors = Le32(boot + 36);

        uint16_t ext_flags = Le16(boot + 40);
        uint16_t version = Le16(boot + 42);
        root_cluster = Le32(boot + 44);

        /*
        * A version we don't know about might have a different layout.
        */
        if (version != 0) {
            return ENOTSUP;
        }

        /*
        * Normally all copies of the FAT are kept the same. But there is an
        * option to just keep one up to date, in which case the others are stale
        * and we have to use that one.
        */
        if (ext_flags & 0x80) {
            active_fat = ext_flags & 0x0F;
            if (active_fat >= fat_count) {
                return ENOTSUP;
            }
        }
    } else {
        /* FAT12/FAT16 have a fixed size root directory. */
        if (root_entries == 0) {
            return ENOTSUP;
        }

        root_dir_sectors = (root_entries * FAT_ENTRY_SIZE + FAT_SECTOR_SIZE - 1) / FAT_SECTOR_SIZE;
    }

    if (reserved_sectors == 0 || fat_count == 0 || fat_sectors == 0) {
        return ENOTSUP;
    }

    uint64_t root_dir_start = (uint64_t) reserved_sectors + (uint64_t) fat_count * fat_sectors;
    uint64_t data_start = root_dir_start + root_dir_sectors;

    if (data_start >= total_sectors) {
        return ENOTSUP;
    }

    uint64_t cluster_count = (total_sectors - data_start) / sectors_per_cluster;

    int type;
    if (is_fat32) {
        type = FAT_TYPE_32;
    } else if (cluster_count < 4085) {
        type = FAT_TYPE_12;
    } else {
        type = FAT_TYPE_16;
    }

    /*
    * Clusters are numbered from 2. The number of the last one is limited by what
    * the entry size can represent (the top few values mean "end of chain" etc.),
    * and by how big the FAT actually is. A volume whose FAT is too small for the
    * size of the data area just has its tail ignored.
    */
    uint64_t max_cluster = cluster_count + 1;

    uint64_t type_limit = (type == FAT_TYPE_12) ? 0xFF6 : (type == FAT_TYPE_16) ? 0xFFF6 : 0x0FFFFFF6;
    max_cluster = MIN(max_cluster, type_limit);
    max_cluster = MIN(max_cluster, fat_table_capacity(type, (uint64_t) fat_sectors * FAT_SECTOR_SIZE));

    if (max_cluster < 2) {
        return ENOTSUP;
    }

    if (is_fat32 && (root_cluster < 2 || root_cluster > max_cluster)) {
        return ENOTSUP;
    }

    struct fat* fat = AllocHeap(sizeof(struct fat));
    if (fat == NULL) {
        return ENOMEM;
    }

    memset(fat, 0, sizeof(struct fat));

    fat->disk = disk;
    fat->type = type;
    fat->sectors_per_cluster = sectors_per_cluster;
    fat->cluster_bytes = sectors_per_cluster * FAT_SECTOR_SIZE;
    fat->root_cluster = root_cluster;
    fat->root_dir_start_sector = (uint32_t) root_dir_start;
    fat->root_dir_sectors = root_dir_sectors;
    fat->data_start_sector = (uint32_t) data_start;
    fat->max_cluster = (uint32_t) max_cluster;
    fat->fat_start_sector = reserved_sectors + active_fat * fat_sectors;
    fat->fat_sectors = fat_sectors;
    fat->msgbox = NULL;
    fat->table_lock = NULL;
    fat->cache_mru = 0;

    for (int i = 0; i < FAT_CACHE_SLOTS; ++i) {
        fat->cache[i].valid = false;
    }

    fat->hint_first_cluster = 0;

    LogPrintf("[fat]: FAT%d volume, %d clusters of %d bytes\n", type, (int) (max_cluster - 1), (int) fat->cluster_bytes);

    *out = fat;
    return 0;
}

/*
* Only valid before fat_start_driver (there is no way to stop the thread).
*/
void fat_destroy(struct fat* fat) {
    FreeHeap(fat);
}
