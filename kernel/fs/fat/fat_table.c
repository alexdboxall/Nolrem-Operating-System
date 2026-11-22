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
*
* Changing an entry changes the cached sector and writes it straight back to the
* disk (to every copy of the FAT that is in use), so the cache never holds
* anything that isn't on the disk already.
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
* The returned pointer is only valid for as long as the lock is held, and until
* the next call to this (which might throw the sector out).
*/
static int FatCacheGet(struct fat* fat, uint32_t sector, struct fat_cache_slot** slot_out) {
    for (int i = 0; i < FAT_CACHE_SLOTS; ++i) {
        if (fat->cache[i].valid && fat->cache[i].sector == sector) {
            fat->cache_mru = i;
            *slot_out = &fat->cache[i];
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
    int status = FatDiskRead(fat->disk, slot->data, FAT_SECTOR_SIZE, byte);
    if (status != 0) {
        return status;
    }

    slot->valid = true;
    slot->sector = sector;
    fat->cache_mru = victim;

    *slot_out = slot;
    return 0;
}

/*
* Writes a (changed) cached sector to the disk, to every copy of the FAT that we
* are keeping up to date. table_lock must be held.
*
* If that fails then the disk and the cache might not agree, so the cache slot is
* thrown away and will be read again when it's next needed.
*/
static int FatCacheWriteBack(struct fat* fat, struct fat_cache_slot* slot) {
    uint32_t copies = fat->fat_mirrored ? fat->fat_count : 1;
    int status = 0;

    /*
    * When the copies are mirrored the one we read from is the first one, so it
    * is also the first one written.
    */
    for (uint32_t i = 0; i < copies && status == 0; ++i) {
        uint32_t start = fat->fat_start_sector + i * fat->fat_sectors;
        uint64_t byte = (uint64_t) (start + slot->sector) * FAT_SECTOR_SIZE;
        status = FatDiskWrite(fat->disk, slot->data, FAT_SECTOR_SIZE, byte);
    }

    if (status != 0) {
        slot->valid = false;
    }

    return status;
}

/*
* Where the entry for a cluster is in the FAT, and how many bytes of the FAT
* have to be looked at to get all of it.
*/
static void FatEntryLocation(const struct fat* fat, uint32_t cluster, uint32_t* offset, uint32_t* length) {
    switch (fat->type) {
    case FAT_TYPE_12:
        /*
        * Entries are 12 bits, so two of them are packed into 3 bytes. Entry N
        * starts at byte N * 1.5. We always use the 2 bytes there, and for odd
        * N the entry is the high 12 bits of them.
        */
        *offset = cluster + cluster / 2;
        *length = 2;
        break;

    case FAT_TYPE_16:
        *offset = cluster * 2;
        *length = 2;
        break;

    default:
        *offset = cluster * 4;
        *length = 4;
        break;
    }
}

/*
* Copies 'length' (at most 4) bytes of the FAT, starting at byte 'offset' of it,
* into 'bytes'. table_lock must be held.
*
* 16 and 32 bit entries are aligned so they are always in one sector. A 12 bit
* entry can have its two bytes in different sectors.
*/
static int FatTableRead(struct fat* fat, uint32_t offset, uint32_t length, uint8_t* bytes) {
    uint32_t sector = offset / FAT_SECTOR_SIZE;
    uint32_t within = offset % FAT_SECTOR_SIZE;
    uint32_t first_part = MIN(length, FAT_SECTOR_SIZE - within);

    if (sector >= fat->fat_sectors) {
        return EIO;
    }

    struct fat_cache_slot* slot;
    int status = FatCacheGet(fat, sector, &slot);
    if (status != 0) {
        return status;
    }

    memcpy(bytes, slot->data + within, first_part);

    if (first_part < length) {
        if (sector + 1 >= fat->fat_sectors) {
            return EIO;
        }

        status = FatCacheGet(fat, sector + 1, &slot);
        if (status != 0) {
            return status;
        }

        memcpy(bytes + first_part, slot->data, length - first_part);
    }

    return 0;
}

/*
* The opposite of FatTableRead: replaces those bytes of the FAT, and writes the
* result to the disk. table_lock must be held.
*/
static int FatTableWrite(struct fat* fat, uint32_t offset, uint32_t length, const uint8_t* bytes) {
    uint32_t sector = offset / FAT_SECTOR_SIZE;
    uint32_t within = offset % FAT_SECTOR_SIZE;
    uint32_t first_part = MIN(length, FAT_SECTOR_SIZE - within);

    if (sector >= fat->fat_sectors) {
        return EIO;
    }

    struct fat_cache_slot* slot;
    int status = FatCacheGet(fat, sector, &slot);
    if (status != 0) {
        return status;
    }

    memcpy(slot->data + within, bytes, first_part);

    status = FatCacheWriteBack(fat, slot);
    if (status != 0) {
        return status;
    }

    if (first_part < length) {
        if (sector + 1 >= fat->fat_sectors) {
            return EIO;
        }

        status = FatCacheGet(fat, sector + 1, &slot);
        if (status != 0) {
            return status;
        }

        memcpy(slot->data, bytes + first_part, length - first_part);

        status = FatCacheWriteBack(fat, slot);
        if (status != 0) {
            return status;
        }
    }

    return 0;
}

/*
* Reads the raw value of the FAT entry for a cluster, as it is stored (with the
* reserved top 4 bits of a FAT32 entry cleared). The cluster must be at most
* max_cluster, which is checked when mounting to be inside the FAT.
*/
static int FatReadEntry(struct fat* fat, uint32_t cluster, uint32_t* value) {
    uint32_t offset;       /* byte offset of the entry within the FAT */
    uint32_t length;       /* how many bytes to look at */
    FatEntryLocation(fat, cluster, &offset, &length);

    uint8_t bytes[4];

    KeAcquireMutex(fat->table_lock, TIMEOUT_INFINITE);
    int status = FatTableRead(fat, offset, length, bytes);
    KeReleaseMutex(fat->table_lock);

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
* The value that marks the last cluster of a chain, when we're the ones writing it.
*/
static uint32_t FatEndOfChain(const struct fat* fat) {
    switch (fat->type) {
    case FAT_TYPE_12: return 0xFFF;
    case FAT_TYPE_16: return 0xFFFF;
    default:          return 0x0FFFFFFF;
    }
}

/*
* Sets the FAT entry for a cluster. Needs write_lock. Bits of the FAT that are
* not part of the entry (the neighbouring entry on FAT12, the reserved top 4 bits
* on FAT32) are left as they were.
*/
static int FatWriteEntry(struct fat* fat, uint32_t cluster, uint32_t value) {
    if (cluster < 2 || cluster > fat->max_cluster) {
        return EIO;
    }

    uint32_t offset;
    uint32_t length;
    FatEntryLocation(fat, cluster, &offset, &length);

    uint8_t bytes[4];
    int status = 0;

    KeAcquireMutex(fat->table_lock, TIMEOUT_INFINITE);

    switch (fat->type) {
    case FAT_TYPE_12:
        status = FatTableRead(fat, offset, length, bytes);
        if (status == 0) {
            uint32_t raw = bytes[0] | ((uint32_t) bytes[1] << 8);
            if (cluster & 1) {
                raw = (raw & 0x000F) | ((value & 0xFFF) << 4);
            } else {
                raw = (raw & 0xF000) | (value & 0xFFF);
            }
            FatPut16(bytes, raw);
        }
        break;

    case FAT_TYPE_16:
        FatPut16(bytes, value);
        break;

    default:
        status = FatTableRead(fat, offset, length, bytes);
        if (status == 0) {
            FatPut32(bytes, (Le32(bytes) & 0xF0000000u) | (value & 0x0FFFFFFFu));
        }
        break;
    }

    if (status == 0) {
        status = FatTableWrite(fat, offset, length, bytes);
    }

    KeReleaseMutex(fat->table_lock);
    return status;
}

/*
* Finds the cluster after 'cluster' in its chain. Returns 0 and the next cluster,
* FAT_END if this is the last cluster, or EIO if the chain is corrupt (this
* includes free, reserved and bad clusters, and clusters that don't exist) or
* the FAT couldn't be read.
*/
int FatNextCluster(struct fat* fat, uint32_t cluster, uint32_t* next) {
    if (cluster < 2 || cluster > fat->max_cluster) {
        return EIO;
    }

    uint32_t entry;
    int status = FatReadEntry(fat, cluster, &entry);
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
int FatClusterAt(struct fat* fat, uint32_t first_cluster, uint32_t index, uint32_t* out) {
    uint32_t cluster = first_cluster;
    uint32_t i = 0;

    if (fat->hint_first_cluster == first_cluster && fat->hint_index <= index) {
        cluster = fat->hint_cluster;
        i = fat->hint_index;
    }

    for (; i < index; ++i) {
        int status = FatNextCluster(fat, cluster, &cluster);
        if (status != 0) {
            return status;
        }
    }

    FatSetHint(fat, first_cluster, index, cluster);

    *out = cluster;
    return 0;
}

void FatSetHint(struct fat* fat, uint32_t first_cluster, uint32_t index, uint32_t cluster) {
    fat->hint_first_cluster = first_cluster;
    fat->hint_index = index;
    fat->hint_cluster = cluster;
}

/*
* The byte offset of the start of a (valid) cluster, from the start of the volume.
*/
uint64_t FatClusterToByte(const struct fat* fat, uint32_t cluster) {
    uint64_t sector = (uint64_t) fat->data_start_sector + (uint64_t) (cluster - 2) * fat->sectors_per_cluster;
    return sector * FAT_SECTOR_SIZE;
}

/*
* ---------------------------------------------------------------------------
* Allocating clusters
* ---------------------------------------------------------------------------
*
* Everything below needs write_lock.
*/

/*
* The FAT32 FSInfo sector has a count of the free clusters and the number of one
* that is probably free. Keeping those right on every allocation would be
* another disk write each time, so instead (once, the first time we allocate
* something) both are set to 0xFFFFFFFF, which is how a volume says "unknown,
* work it out yourself".
*/
static int FatInvalidateFsinfo(struct fat* fat) {
    if (fat->fsinfo_done) {
        return 0;
    }

    if (fat->fsinfo_sector != 0) {
        uint8_t sector[FAT_SECTOR_SIZE];
        uint64_t byte = (uint64_t) fat->fsinfo_sector * FAT_SECTOR_SIZE;

        int status = FatDiskRead(fat->disk, sector, FAT_SECTOR_SIZE, byte);
        if (status != 0) {
            return status;
        }

        bool valid = Le32(sector) == 0x41615252u
                  && Le32(sector + 484) == 0x61417272u
                  && Le32(sector + 508) == 0xAA550000u;

        if (valid && (Le32(sector + 488) != 0xFFFFFFFFu || Le32(sector + 492) != 0xFFFFFFFFu)) {
            FatPut32(sector + 488, 0xFFFFFFFFu);
            FatPut32(sector + 492, 0xFFFFFFFFu);

            status = FatDiskWrite(fat->disk, sector, FAT_SECTOR_SIZE, byte);
            if (status != 0) {
                return status;
            }
        }
    }

    fat->fsinfo_done = true;
    return 0;
}

/*
* Finds a free cluster, and marks it as the end of a chain (so it is no longer
* free, and is a valid chain of one cluster). The search starts at 'near' and
* wraps around, so passing the cluster after a file's last one keeps its data
* together. 0 starts where the last allocation left off.
*
* Returns ENOSPC if there aren't any. The contents of the cluster are whatever
* was on the disk before.
*/
int FatAllocCluster(struct fat* fat, uint32_t near, uint32_t* out) {
    int status = FatInvalidateFsinfo(fat);
    if (status != 0) {
        return status;
    }

    uint32_t cluster = near;
    if (cluster < 2 || cluster > fat->max_cluster) {
        cluster = fat->alloc_hint;
    }
    if (cluster < 2 || cluster > fat->max_cluster) {
        cluster = 2;
    }

    /* Clusters are numbered from 2 to max_cluster. */
    for (uint32_t tried = 0; tried < fat->max_cluster - 1; ++tried) {
        uint32_t entry;
        status = FatReadEntry(fat, cluster, &entry);
        if (status != 0) {
            return status;
        }

        if (entry == 0) {
            status = FatWriteEntry(fat, cluster, FatEndOfChain(fat));
            if (status != 0) {
                return status;
            }

            fat->alloc_hint = (cluster == fat->max_cluster) ? 2 : cluster + 1;
            *out = cluster;
            return 0;
        }

        cluster = (cluster == fat->max_cluster) ? 2 : cluster + 1;
    }

    return ENOSPC;
}

/*
* Makes 'next' the cluster that follows 'cluster' in its chain.
*/
int FatSetNext(struct fat* fat, uint32_t cluster, uint32_t next) {
    if (cluster < 2 || cluster > fat->max_cluster || next < 2 || next > fat->max_cluster) {
        return EIO;
    }

    return FatWriteEntry(fat, cluster, next);
}

/*
* Gives back a cluster that was allocated but is not (or no longer) part of
* anything.
*/
void FatFreeCluster(struct fat* fat, uint32_t cluster) {
    FatWriteEntry(fat, cluster, 0);       /* Nothing to do if that fails, but leak the cluster. */
}

/*
* Adds a new cluster to the end of a chain, whose current last cluster is 'last'.
* The contents of the new cluster are whatever was on the disk before.
*/
int FatAppendCluster(struct fat* fat, uint32_t last, uint32_t* out) {
    uint32_t fresh;
    int status = FatAllocCluster(fat, last + 1, &fresh);
    if (status != 0) {
        return status;
    }

    status = FatSetNext(fat, last, fresh);
    if (status != 0) {
        FatFreeCluster(fat, fresh);
        return status;
    }

    *out = fresh;
    return 0;
}

/*
* Cuts the chain that starts at *first_cluster down to 'keep' clusters, and gives
* the clusters after that back. Used to tidy up after a write that failed part
* way, which can leave clusters on the end that nothing ended up in. (If keep is
* 0, the whole chain goes, and *first_cluster becomes 0.)
*
* A chain that is already no longer than that is left alone.
*
* Only for the filesystem thread, as it uses the seek hint.
*/
int FatTrimChain(struct fat* fat, uint32_t* first_cluster, uint32_t keep) {
    if (*first_cluster == 0) {
        return 0;
    }

    uint32_t tail;
    int status;

    if (keep == 0) {
        tail = *first_cluster;
    } else {
        uint32_t last_kept;
        status = FatClusterAt(fat, *first_cluster, keep - 1, &last_kept);
        if (status == FAT_END) {
            return 0;
        } else if (status != 0) {
            return status;
        }

        status = FatNextCluster(fat, last_kept, &tail);
        if (status == FAT_END) {
            return 0;
        } else if (status != 0) {
            return status;
        }

        /* End the chain here, before freeing the rest, so the FAT is never left pointing at a free cluster. */
        status = FatWriteEntry(fat, last_kept, FatEndOfChain(fat));
        if (status != 0) {
            return status;
        }
    }

    /* Whatever happens, what the hint points at may be about to go. */
    FatSetHint(fat, 0, 0, 0);

    if (keep == 0) {
        *first_cluster = 0;
    }

    /* Bounded by the number of clusters, in case the chain loops. */
    for (uint32_t n = 0; n < fat->max_cluster; ++n) {
        uint32_t next;
        status = FatNextCluster(fat, tail, &next);
        int free_status = FatWriteEntry(fat, tail, 0);
        if (free_status != 0) {
            return free_status;
        }

        if (status == FAT_END) {
            return 0;
        } else if (status != 0) {
            return status;
        }

        tail = next;
    }

    return 0;
}

/*
* Like FatClusterAt, but a chain that is too short is made longer instead of
* returning FAT_END. A file that has no clusters at all (*first_cluster is 0) is
* given one, and *first_cluster is updated. (It's up to the caller to record
* that in the directory entry.)
*
* The new clusters are not cleared.
*
* Only for the filesystem thread, as it uses and updates the seek hint.
*/
int FatClusterAtExtend(struct fat* fat, uint32_t* first_cluster, uint32_t index, uint32_t* out) {
    int status;

    if (*first_cluster == 0) {
        status = FatAllocCluster(fat, 0, first_cluster);
        if (status != 0) {
            return status;
        }
    } else if (*first_cluster < 2 || *first_cluster > fat->max_cluster) {
        return EIO;
    }

    uint32_t cluster = *first_cluster;
    uint32_t i = 0;

    if (fat->hint_first_cluster == *first_cluster && fat->hint_index <= index) {
        cluster = fat->hint_cluster;
        i = fat->hint_index;
    }

    for (; i < index; ++i) {
        uint32_t next;
        status = FatNextCluster(fat, cluster, &next);
        if (status == FAT_END) {
            status = FatAppendCluster(fat, cluster, &next);
        }
        if (status != 0) {
            return status;
        }

        cluster = next;
    }

    FatSetHint(fat, *first_cluster, index, cluster);

    *out = cluster;
    return 0;
}
