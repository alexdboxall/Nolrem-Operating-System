
/*
* fat_file.c - reading file data
*/

#include <common.h>
#include <errno.h>
#include <vfs.h>
#include <string.h>
#include <assert.h>
#include <transfer.h>
#include <fs/fat/fat_private.h>

static int fat_read_linear(struct fat* fat, uint64_t disk_byte, uint64_t count, struct transfer* io) {
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

            int status = fat_disk_transfer(fat->disk, &disk_io);
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

            int status = fat_disk_read(fat->disk, sector_buffer, FAT_SECTOR_SIZE, disk_byte - sector_offset);
            if (status != 0) {
                return status;
            }

            size_t transfer_size = MIN((uint64_t) FAT_SECTOR_SIZE - sector_offset, count);

            /* This advances our transfer. */
            status = PerformTransfer(sector_buffer + sector_offset, io, transfer_size);
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
int fat_read_file_sync(struct fat* fat, uint32_t first_cluster, uint32_t file_size, struct transfer* io) {
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

    int status = fat_cluster_at(fat, first_cluster, index, &cluster);
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
            status = fat_next_cluster(fat, last, &next_cluster);
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
        status = fat_read_linear(fat, fat_cluster_to_byte(fat, cluster) + within, count, io);
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
        fat_set_hint(fat, first_cluster, index + hops, cluster + hops);

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