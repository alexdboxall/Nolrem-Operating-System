/*
* fat_table.c - the File Allocation Table
*
* The FAT is an array with one entry per cluster, saying which cluster comes next
* in the file (or directory) that the cluster belongs to. It can be megabytes in
* size, so it isn't loaded into memory. Instead the two most recently used
* sectors of it are kept in a small cache.
*
* Looking at a FAT entry can therefore mean a disk read, which is why this can
* fail with an I/O error where a plain array lookup couldn't.
*/

#include <common.h>
#include <errno.h>
#include <vfs.h>
#include <string.h>
#include <assert.h>
#include <mutex.h>
#include <fs/fat/fat_private.h>

/*
* Makes the given sector of the FAT (counting from the first sector of it) available
* in the cache. table_lock must be held.
*
* The returned pointer is only valid for as long as the lock is held.
*/
static int fat_cache_get(struct fat* fat, uint32_t sector, const uint8_t** data_out) {
    for (int i = 0; i < FAT_CACHE_SLOTS; ++i) {
        if (fat->cache[i].valid && fat->cache[i].sector == sector) {
            fat->cache_mru = i;
            *data_out = fat->cache[i].data;
            return 0;
        }
    }

    /*
    * Not there. Use an empty slot if there is one, otherwise throw out the one
    * that was used the longest ago. (With two slots, that's the one that isn't
    * the most recently used.)
    */
    int victim = 0;
    if (!fat->cache[0].valid) {
        victim = 0;
    } else if (!fat->cache[1].valid) {
        victim = 1;
    } else {
        victim = 1 - fat->cache_mru;
    }

    struct fat_cache_slot* slot = &fat->cache[victim];

    /*
    * If the read fails, the slot must not claim to have the sector.
    */
    slot->valid = false;

    uint64_t byte = (uint64_t) (fat->fat_start_sector + sector) * FAT_SECTOR_SIZE;
    int status = fat_disk_read(fat->disk, slot->data, FAT_SECTOR_SIZE, byte);
    if (status != 0) {
        return status;
    }

    slot->valid = true;
    slot->sector = sector;
    fat->cache_mru = victim;

    *data_out = slot->data;
    return 0;
}

/*
* Reads the raw value of the FAT entry for a cluster, as it is stored (with the
* reserved top 4 bits of a FAT32 entry cleared). The cluster must be at most
* max_cluster, which is checked when mounting to be inside the FAT.
*/
static int fat_read_entry(struct fat* fat, uint32_t cluster, uint32_t* value) {
    uint32_t offset;       /* byte offset of the entry within the FAT */
    uint32_t length;       /* how many bytes to look at */

    switch (fat->type) {
    case FAT_TYPE_12:
        /*
        * Entries are 12 bits, so two of them are packed into 3 bytes. Entry N
        * starts at byte N * 1.5. We always read the 2 bytes there, and for odd
        * N the entry is the high 12 bits of them.
        */
        offset = cluster + cluster / 2;
        length = 2;
        break;

    case FAT_TYPE_16:
        offset = cluster * 2;
        length = 2;
        break;

    default:
        offset = cluster * 4;
        length = 4;
        break;
    }

    uint8_t bytes[4];

    AcquireMutex(fat->table_lock, TIMEOUT_INFINITE);

    /*
    * 16 and 32 bit entries are aligned so they are always in one sector. A 12
    * bit entry can have its two bytes in different sectors.
    */
    uint32_t sector = offset / FAT_SECTOR_SIZE;
    uint32_t within = offset % FAT_SECTOR_SIZE;
    uint32_t first_part = MIN(length, FAT_SECTOR_SIZE - within);

    int status = 0;
    const uint8_t* data = NULL;

    if (sector >= fat->fat_sectors) {
        status = EIO;
    } else {
        status = fat_cache_get(fat, sector, &data);
    }

    if (status == 0) {
        memcpy(bytes, data + within, first_part);

        if (first_part < length) {
            if (sector + 1 >= fat->fat_sectors) {
                status = EIO;
            } else {
                status = fat_cache_get(fat, sector + 1, &data);
            }

            if (status == 0) {
                memcpy(bytes + first_part, data, length - first_part);
            }
        }
    }

    ReleaseMutex(fat->table_lock);

    if (status != 0) {
        return status;
    }

    switch (fat->type) {
    case FAT_TYPE_12: {
        uint32_t raw = bytes[0] | ((uint32_t) bytes[1] << 8);
        *value = (cluster & 1) ? (raw >> 4) : (raw & 0xFFF);
        break;
    }

    case FAT_TYPE_16:
        *value = Le16(bytes);
        break;

    default:
        /* The top 4 bits of a FAT32 entry are reserved and must be ignored. */
        *value = Le32(bytes) & 0x0FFFFFFF;
        break;
    }

    return 0;
}

/*
* Finds the cluster after 'cluster' in its chain. Returns 0 and the next cluster,
* FAT_END if this is the last cluster, or EIO if the chain is corrupt (this
* includes free, reserved and bad clusters, and clusters that don't exist) or
* the FAT couldn't be read.
*/
int fat_next_cluster(struct fat* fat, uint32_t cluster, uint32_t* next) {
    if (cluster < 2 || cluster > fat->max_cluster) {
        return EIO;
    }

    uint32_t entry;
    int status = fat_read_entry(fat, cluster, &entry);
    if (status != 0) {
        return status;
    }

    uint32_t end_marker;
    switch (fat->type) {
    case FAT_TYPE_12: end_marker = 0xFF8; break;
    case FAT_TYPE_16: end_marker = 0xFFF8; break;
    default:          end_marker = 0x0FFFFFF8; break;
    }

    if (entry >= end_marker) {
        return FAT_END;
    }

    /*
    * The bad cluster marker (0xFF7 etc.) and the reserved values around it are
    * larger than max_cluster, so they are caught here too.
    */
    if (entry < 2 || entry > fat->max_cluster) {
        return EIO;
    }

    *next = entry;
    return 0;
}

/*
* Finds the cluster with the given index in the chain that starts at
* first_cluster (index 0 is first_cluster itself). Returns FAT_END if the chain
* is shorter than that.
*
* Only for the filesystem thread, as it uses and updates the seek hint.
*/
int fat_cluster_at(struct fat* fat, uint32_t first_cluster, uint32_t index, uint32_t* out) {
    uint32_t cluster = first_cluster;
    uint32_t i = 0;

    if (fat->hint_first_cluster == first_cluster && fat->hint_index <= index) {
        cluster = fat->hint_cluster;
        i = fat->hint_index;
    }

    for (; i < index; ++i) {
        int status = fat_next_cluster(fat, cluster, &cluster);
        if (status != 0) {
            return status;
        }
    }

    fat_set_hint(fat, first_cluster, index, cluster);

    *out = cluster;
    return 0;
}

void fat_set_hint(struct fat* fat, uint32_t first_cluster, uint32_t index, uint32_t cluster) {
    fat->hint_first_cluster = first_cluster;
    fat->hint_index = index;
    fat->hint_cluster = cluster;
}

/*
* The byte offset of the start of a (valid) cluster, from the start of the volume.
*/
uint64_t fat_cluster_to_byte(const struct fat* fat, uint32_t cluster) {
    uint64_t sector = (uint64_t) fat->data_start_sector + (uint64_t) (cluster - 2) * fat->sectors_per_cluster;
    return sector * FAT_SECTOR_SIZE;
}