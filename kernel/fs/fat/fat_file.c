
/*
* fat_file.c - reading and writing file data
*/

#include <common.h>
#include <errno.h>
#include <vfs.h>
#include <string.h>
#include <assert.h>
#include <transfer.h>
#include <mutex.h>
#include <fs/fat/fat_private.h>

static int FatReadLinear(struct fat* fat, uint64_t disk_byte, uint64_t count, struct transfer* io) {
    while (count != 0) {
        uint64_t sector_offset = disk_byte % FAT_SECTOR_SIZE;

        if (sector_offset == 0 && count >= FAT_SECTOR_SIZE) {
            /*
            * Aligned, so transfer as many whole sectors as we can directly.
            *
            * We need the disk offset, not the file offset, so the disk transfer
            * is a copy of ours with the offset replaced. Ours is advanced by
            * however much the disk actually transferred.
            */
            uint64_t length = count - count % FAT_SECTOR_SIZE;

            struct transfer disk_io = *io;
            disk_io.offset = disk_byte;
            disk_io.length_remaining = length;

            int status = FatDiskTransfer(fat->disk, &disk_io);
            if (status != 0) {
                return status;
            }

            uint64_t transferred = length - disk_io.length_remaining;
            if (transferred == 0) {
                /* No progress would mean looping forever. */
                return EIO;
            }

            io->offset += transferred;
            io->length_remaining -= transferred;
            io->address = ((uint8_t*) io->address) + transferred;

            disk_byte += transferred;
            count -= transferred;

        } else {
            /*
            * A partial sector. Read the whole sector into an internal buffer,
            * and then copy the part we want to the destination.
            */
            uint8_t sector_buffer[FAT_SECTOR_SIZE];

            int status = FatDiskRead(fat->disk, sector_buffer, FAT_SECTOR_SIZE, disk_byte - sector_offset);
            if (status != 0) {
                return status;
            }

            size_t transfer_size = MIN((uint64_t) FAT_SECTOR_SIZE - sector_offset, count);

            /* This advances our transfer. */
            status = KePerformTransfer(sector_buffer + sector_offset, io, transfer_size);
            if (status != 0) {
                return status;
            }

            disk_byte += transfer_size;
            count -= transfer_size;
        }
    }

    return 0;
}

/*
* Does the actual work of reading from a file into the transfer. Runs on the
* filesystem thread.
*
* A file is a chain of clusters. Clusters that happen to be next to each other
* on the disk (which is the usual case) are read with one transfer.
*/
int FatReadFileSync(struct fat* fat, struct fat_file* file, struct transfer* io) {
    /*
    * These are looked at now rather than when the read was queued, so that a
    * write that was queued before us is taken into account.
    */
    uint32_t first_cluster = file->first_cluster;
    uint32_t file_size = file->size;

    if ((uint64_t) io->offset >= file_size) {
        /* End of file, nothing to transfer. */
        return 0;
    }

    uint32_t size_left = file_size - io->offset;

    if (first_cluster < 2 || first_cluster > fat->max_cluster) {
        /* Not empty, but has no (valid) data. */
        return EIO;
    }

    /*
    * Find the cluster that the offset is in. 'index' is how far along the chain
    * it is (0 for the first cluster of the file).
    */
    uint32_t index = (uint32_t) ((uint64_t) io->offset / fat->cluster_bytes);
    uint32_t within = (uint32_t) ((uint64_t) io->offset % fat->cluster_bytes);
    uint32_t cluster;

    int status = FatClusterAt(fat, first_cluster, index, &cluster);
    if (status == FAT_END) {
        /* The file is shorter than its size says. */
        return EIO;
    } else if (status != 0) {
        return status;
    }

    while (io->length_remaining != 0 && size_left != 0) {
        uint64_t want = MIN((uint64_t) io->length_remaining, (uint64_t) size_left);

        /*
        * Work out how many bytes we can read from here in one go, by following
        * the chain for as long as it keeps going to the next cluster along.
        */
        uint64_t run_bytes = fat->cluster_bytes - within;
        uint32_t last = cluster;
        uint32_t next_cluster = 0;
        bool chain_ended = false;

        while (run_bytes < want) {
            status = FatNextCluster(fat, last, &next_cluster);
            if (status == FAT_END) {
                chain_ended = true;
                break;
            } else if (status != 0) {
                return status;
            }

            if (next_cluster != last + 1) {
                break;
            }

            last = next_cluster;
            run_bytes += fat->cluster_bytes;
        }

        uint64_t count = MIN(want, run_bytes);
        status = FatReadLinear(fat, FatClusterToByte(fat, cluster) + within, count, io);
        if (status != 0) {
            return status;
        }

        /*
        * Remember where we got to, so a read that carries on from here doesn't
        * have to start from the beginning of the chain. The run is made of
        * consecutive clusters, so the one holding the last byte we read is
        * just 'hops' further along.
        */
        uint32_t hops = (uint32_t) ((within + count - 1) / fat->cluster_bytes);
        FatSetHint(fat, first_cluster, index + hops, cluster + hops);

        size_left -= count;
        if (io->length_remaining == 0 || size_left == 0) {
            break;
        }

        if (chain_ended) {
            /* The file is shorter than its size says. */
            return EIO;
        }

        index += (last - cluster) + 1;
        cluster = next_cluster;
        within = 0;
    }

    return 0;
}
/*
* ---------------------------------------------------------------------------
* Writing
* ---------------------------------------------------------------------------
*/

/*
* The write version of FatReadLinear: writes 'count' bytes to a stretch of the
* disk that is contiguous, taking them from the transfer. If 'io' is NULL it
* writes zeros instead.
*/
static int FatWriteLinear(struct fat* fat, uint64_t disk_byte, uint64_t count, struct transfer* io) {
    while (count != 0) {
        uint64_t sector_offset = disk_byte % FAT_SECTOR_SIZE;

        if (sector_offset == 0 && count >= FAT_SECTOR_SIZE) {
            /*
            * Aligned, so transfer as many whole sectors as we can directly (see
            * FatReadLinear for how the transfers work).
            */
            uint64_t length = count - count % FAT_SECTOR_SIZE;

            if (io == NULL) {
                int status = FatDiskZero(fat->disk, disk_byte, length);
                if (status != 0) {
                    return status;
                }

                disk_byte += length;
                count -= length;
                continue;
            }

            struct transfer disk_io = *io;
            disk_io.offset = disk_byte;
            disk_io.length_remaining = length;

            int status = FatDiskWriteTransfer(fat->disk, &disk_io);
            if (status != 0) {
                return status;
            }

            uint64_t transferred = length - disk_io.length_remaining;
            if (transferred == 0) {
                /* No progress would mean looping forever. */
                return EIO;
            }

            io->offset += transferred;
            io->length_remaining -= transferred;
            io->address = ((uint8_t*) io->address) + transferred;

            disk_byte += transferred;
            count -= transferred;

        } else {
            /*
            * Only part of a sector is being written, but the disk can only do
            * whole sectors. So read the sector, change the part that we're
            * writing, and write it back.
            */
            uint8_t sector_buffer[FAT_SECTOR_SIZE];
            uint64_t sector_start = disk_byte - sector_offset;

            int status = FatDiskRead(fat->disk, sector_buffer, FAT_SECTOR_SIZE, sector_start);
            if (status != 0) {
                return status;
            }

            size_t transfer_size = MIN((uint64_t) FAT_SECTOR_SIZE - sector_offset, count);

            if (io != NULL) {
                /* This advances our transfer. */
                status = KePerformTransfer(sector_buffer + sector_offset, io, transfer_size);
                if (status != 0) {
                    return status;
                }
            } else {
                memset(sector_buffer + sector_offset, 0, transfer_size);
            }

            status = FatDiskWrite(fat->disk, sector_buffer, FAT_SECTOR_SIZE, sector_start);
            if (status != 0) {
                return status;
            }

            disk_byte += transfer_size;
            count -= transfer_size;
        }
    }

    return 0;
}

/*
* Writes 'count' bytes of the file starting at byte 'start' of it, from the
* transfer (or zeros, if 'io' is NULL). Clusters are added to the file as needed.
* This doesn't change the file's size, that's for the caller to do (according to
* how much of the transfer got used).
*
* Like reading, the clusters that are next to each other on the disk (which for
* newly added clusters is the usual case) are written with one transfer.
*
* If that runs out of space (or fails in some other way) part way through, then
* whatever fitted has been written. The data goes to the disk before the caller
* gets to change the size, so nothing is ever visible in the file that wasn't
* written.
*/
static int FatWriteRange(struct fat* fat, struct fat_file* file, uint64_t start, uint64_t count, struct transfer* io) {
    uint32_t index = (uint32_t) (start / fat->cluster_bytes);
    uint32_t within = (uint32_t) (start % fat->cluster_bytes);
    uint32_t cluster;

    int status = FatClusterAtExtend(fat, &file->first_cluster, index, &cluster);
    if (status != 0) {
        return status;
    }

    while (count != 0) {
        /*
        * Work out how many bytes we can write from here in one go, by following
        * the chain for as long as it keeps going to the next cluster along, and
        * adding to it when it runs out.
        */
        uint64_t run_bytes = fat->cluster_bytes - within;
        uint32_t last = cluster;
        uint32_t next_cluster = 0;
        int extend_status = 0;

        while (run_bytes < count) {
            status = FatNextCluster(fat, last, &next_cluster);
            if (status == FAT_END) {
                status = FatAppendCluster(fat, last, &next_cluster);
            }

            if (status != 0) {
                /* Still write what fits in the run so far. */
                extend_status = status;
                break;
            }

            if (next_cluster != last + 1) {
                break;
            }

            last = next_cluster;
            run_bytes += fat->cluster_bytes;
        }

        uint64_t chunk = MIN(count, run_bytes);
        status = FatWriteLinear(fat, FatClusterToByte(fat, cluster) + within, chunk, io);
        if (status != 0) {
            return status;
        }

        /* Remember where we got to (see FatReadFileSync). */
        uint32_t hops = (uint32_t) ((within + chunk - 1) / fat->cluster_bytes);
        FatSetHint(fat, file->first_cluster, index + hops, cluster + hops);

        count -= chunk;
        if (count == 0) {
            break;
        }

        if (extend_status != 0) {
            return extend_status;
        }

        index += (last - cluster) + 1;
        cluster = next_cluster;
        within = 0;
    }

    return 0;
}

/*
* Does the actual work of writing to a file from the transfer. Runs on the
* filesystem thread.
*
* Writing past the end makes the file longer. If that starts after the old end
* (so there's a gap) the gap reads as zeros. At most 4GiB - 1 bytes fit in a
* file, so a write that goes beyond that is cut short.
*
* If something goes wrong after part of the data has been written, that part
* stays written, and this reports success with the rest of the transfer left
* undone (the same as a short write to a pipe or a full disk would do). The
* error shows up if the caller carries on and tries again.
*/
int FatWriteFileSync(struct fat* fat, struct fat_file* file, struct transfer* io) {
    if (io->length_remaining == 0) {
        return 0;
    }

    uint64_t start = io->offset;
    if (start >= FAT_MAX_FILE_SIZE) {
        return EFBIG;
    }

    if (file->first_cluster == 0 && file->size != 0) {
        /* Not empty, but has no data. (See FatReadFileSync.) */
        return EIO;
    }

    uint64_t total = MIN((uint64_t) io->length_remaining, FAT_MAX_FILE_SIZE - start);

    KeAcquireMutex(fat->write_lock, TIMEOUT_INFINITE);

    uint32_t old_first_cluster = file->first_cluster;
    uint32_t old_size = file->size;

    int status = 0;

    if (start > file->size) {
        /*
        * Everything between the old end and where we're writing has to read as
        * zeros. (The disk has whatever was there before in it, as clusters are
        * not cleared when they're given to a file.) The size only goes up when
        * all of it is done, so nobody can see a half cleared gap.
        */
        status = FatWriteRange(fat, file, file->size, start - file->size, NULL);
        if (status == 0) {
            file->size = (uint32_t) start;
        }
    }

    if (status == 0) {
        status = FatWriteRange(fat, file, start, total, io);

        /* The transfer has moved on by however much was done. */
        uint64_t end = io->offset;
        if (end > file->size) {
            file->size = (uint32_t) end;
        }
    }

    if (status != 0) {
        /*
        * Clusters are added before the data that goes in them is written, so
        * the chain can now be longer than the file needs. Give the extras back
        * (if that fails too then they're just wasted space), but never any the
        * file had to begin with.
        */
        uint32_t keep = (uint32_t) (((uint64_t) file->size + fat->cluster_bytes - 1) / fat->cluster_bytes);
        if (keep == 0 && old_first_cluster != 0) {
            keep = 1;
        }

        FatTrimChain(fat, &file->first_cluster, keep);
    }

    /*
    * The clusters are linked in the FAT as they are added, but the directory
    * entry is the only record of where a file starts and how long it is.
    */
    int entry_status = 0;
    if (file->first_cluster != old_first_cluster || file->size != old_size) {
        entry_status = FatUpdateEntry(fat, file);
    }

    KeReleaseMutex(fat->write_lock);

    if (status != 0 && io->offset > start) {
        status = 0;
    }

    return (status != 0) ? status : entry_status;
}

/*
* Does the actual work of making a file the given size. Runs on the filesystem
* thread.
*
* Making it shorter gives the clusters it no longer needs back. Making it longer
* adds zeros, which unlike on most filesystems takes up space for all of them as
* FAT has no holes. (This is the same as writing past the end.)
*/
int FatTruncateFileSync(struct fat* fat, struct fat_file* file, uint64_t new_size) {
    if (new_size > FAT_MAX_FILE_SIZE) {
        return EFBIG;
    }

    if (file->first_cluster == 0 && file->size != 0) {
        /* Not empty, but has no data. (See FatReadFileSync.) */
        return EIO;
    }

    KeAcquireMutex(fat->write_lock, TIMEOUT_INFINITE);

    uint32_t old_first_cluster = file->first_cluster;
    uint32_t old_size = file->size;
    int status = 0;

    if (new_size > old_size) {
        status = FatWriteRange(fat, file, old_size, new_size - old_size, NULL);
        if (status == 0) {
            file->size = (uint32_t) new_size;
        } else {
            /* Give back what was added (see FatWriteFileSync). */
            uint32_t keep = (uint32_t) (((uint64_t) file->size + fat->cluster_bytes - 1) / fat->cluster_bytes);
            if (keep == 0 && old_first_cluster != 0) {
                keep = 1;
            }

            FatTrimChain(fat, &file->first_cluster, keep);
        }

        if (file->first_cluster != old_first_cluster || file->size != old_size) {
            int entry_status = FatUpdateEntry(fat, file);
            status = (status != 0) ? status : entry_status;
        }

    } else if (new_size < old_size) {
        uint32_t keep = (uint32_t) ((new_size + fat->cluster_bytes - 1) / fat->cluster_bytes);
        uint32_t chain_start = file->first_cluster;

        file->size = (uint32_t) new_size;
        if (keep == 0) {
            file->first_cluster = 0;
        }

        /*
        * The directory entry is changed first. If something goes wrong after that
        * then there are clusters that nothing is using, which is better than the
        * other way round, where an entry refers to clusters that have been given
        * to someone else.
        */
        status = FatUpdateEntry(fat, file);
        if (status != 0) {
            file->size = old_size;
            file->first_cluster = old_first_cluster;
        } else {
            status = FatTrimChain(fat, &chain_start, keep);
        }
    }

    KeReleaseMutex(fat->write_lock);

    return status;
}
