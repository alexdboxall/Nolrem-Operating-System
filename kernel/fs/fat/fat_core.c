/*
* fat_core.c - FAT12/FAT16 (read only)
*
* Mounting, the FAT itself, directories, file data and the filesystem thread.
* There is no FAT32 support (no cluster chain root directory, 32 bit FAT
* entries) and no long file name support: long name entries are skipped, so
* files are only ever seen by their 8.3 short name.
*
* Like demofs, each mount has its own thread and message box. The vnode read
* operations just post a request and return, the thread does the work
* synchronously, and then calls SignalDoneIO. Follow is synchronous and runs
* on the caller's thread, which is fine because struct fat is read-only once
* mounted.
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
#include <thread.h>
#include <vmm.h>
#include <scheduler.h>
#include <msgbox.h>
#include <sys/types.h>
#include <dirent.h>
#include <fs/fat/fat_private.h>

/*
* How many requests can be queued for the filesystem thread at once.
*/
#define FAT_QUEUE_DEPTH 16

/*
* A FAT directory has at most 65536 entries of 32 bytes. Anything beyond that
* means a corrupt (probably looping) cluster chain.
*/
#define FAT_MAX_DIR_SECTORS 4096

/*
* The FAT is read in pieces of this many sectors when mounting.
*/
#define FAT_LOAD_CHUNK_SECTORS 32

#define FAT_ENTRY_SIZE         32
#define FAT_ENTRIES_PER_SECTOR (FAT_SECTOR_SIZE / FAT_ENTRY_SIZE)

#define ATTR_VOLUME_ID  0x08
#define ATTR_DIRECTORY  0x10
#define ATTR_LFN_MASK   0x3F
#define ATTR_LFN        0x0F    /* Read only + hidden + system + volume ID. Marks a long file name entry. */

#define NT_LOWER_NAME   0x08    /* Windows NT: the name is lower case. */
#define NT_LOWER_EXT    0x10    /* Windows NT: the extension is lower case. */

static uint16_t Le16(const uint8_t* p) {
    return (uint16_t) (p[0] | (p[1] << 8));
}

static uint32_t Le32(const uint8_t* p) {
    return (uint32_t) p[0] | ((uint32_t) p[1] << 8) | ((uint32_t) p[2] << 16) | ((uint32_t) p[3] << 24);
}

/*
* ---------------------------------------------------------------------------
* Synchronous disk access
* ---------------------------------------------------------------------------
*/

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
* Synchronous read from the disk. On return, io has been advanced by however much
* was transferred (like the aio's transfer is by the driver).
*/
static int fat_disk_transfer(struct file* disk, struct transfer* io) {
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
*/
static int fat_disk_read(struct file* disk, void* buffer, size_t length, uint64_t offset) {
    struct transfer io = CreateKernelTransfer(buffer, length, offset, TRANSFER_READ);

    int status = fat_disk_transfer(disk, &io);
    if (status == 0 && io.length_remaining != 0) {
        /* A short read means something is wrong with the disk. */
        status = EIO;
    }

    return status;
}

/*
* ---------------------------------------------------------------------------
* Mounting
* ---------------------------------------------------------------------------
*/

/*
* Checks that the disk holds a FAT12 or FAT16 volume, and if so, reads in the
* FAT. Returns ENOTSUP if it isn't one of those (this includes FAT32).
*
* The type is decided purely by the number of data clusters, as Microsoft's
* specification says. The "FAT12" / "FAT16" label in the boot sector is only
* informational and is often wrong.
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

    /*
    * FAT32 has no fixed size root directory and keeps its FAT size somewhere
    * else, so these being zero means FAT32.
    */
    if (reserved_sectors == 0 || fat_count == 0 || root_entries == 0 || fat_sectors == 0) {
        return ENOTSUP;
    }

    uint32_t root_dir_sectors = (root_entries * FAT_ENTRY_SIZE + FAT_SECTOR_SIZE - 1) / FAT_SECTOR_SIZE;
    uint32_t root_dir_start = reserved_sectors + fat_count * fat_sectors;
    uint32_t data_start = root_dir_start + root_dir_sectors;

    if (total_sectors <= data_start) {
        return ENOTSUP;
    }

    uint32_t cluster_count = (total_sectors - data_start) / sectors_per_cluster;

    int type;
    if (cluster_count == 0) {
        return ENOTSUP;
    } else if (cluster_count < 4085) {
        type = FAT_TYPE_12;
    } else if (cluster_count < 65525) {
        type = FAT_TYPE_16;
    } else {
        /* FAT32 */
        return ENOTSUP;
    }

    /*
    * Clusters are numbered from 2.
    */
    uint32_t max_cluster = cluster_count + 1;

    /*
    * Work out how much of the FAT we need to look at to cover every cluster.
    * FAT12 entries are 12 bits, and reading one touches 2 bytes.
    */
    uint32_t needed = (type == FAT_TYPE_12) ? (max_cluster + max_cluster / 2 + 2) : ((max_cluster + 1) * 2);
    uint32_t load_bytes = (needed + FAT_SECTOR_SIZE - 1) / FAT_SECTOR_SIZE * FAT_SECTOR_SIZE;

    if (load_bytes > fat_sectors * FAT_SECTOR_SIZE) {
        /* The FAT is too small to describe all the clusters. */
        return ENOTSUP;
    }

    struct fat* fat = AllocHeap(sizeof(struct fat));
    if (fat == NULL) {
        return ENOMEM;
    }

    fat->table = AllocHeap(load_bytes);
    if (fat->table == NULL) {
        FreeHeap(fat);
        return ENOMEM;
    }

    fat->disk = disk;
    fat->type = type;
    fat->sectors_per_cluster = sectors_per_cluster;
    fat->cluster_bytes = sectors_per_cluster * FAT_SECTOR_SIZE;
    fat->root_dir_start_sector = root_dir_start;
    fat->root_dir_sectors = root_dir_sectors;
    fat->data_start_sector = data_start;
    fat->max_cluster = max_cluster;
    fat->msgbox = NULL;

    /*
    * We only ever read, so just load the first copy of the FAT and keep it.
    * It is read in smaller pieces so no individual transfer is huge.
    */
    uint64_t fat_start_byte = (uint64_t) reserved_sectors * FAT_SECTOR_SIZE;
    uint32_t loaded = 0;
    while (loaded < load_bytes) {
        uint32_t chunk = MIN(load_bytes - loaded, FAT_LOAD_CHUNK_SECTORS * FAT_SECTOR_SIZE);

        int status = fat_disk_read(disk, fat->table + loaded, chunk, fat_start_byte + loaded);
        if (status != 0) {
            fat_destroy(fat);
            return status;
        }

        loaded += chunk;
    }

    LogPrintf("[fat]: FAT%d volume, %d clusters of %d bytes\n", type, (int) cluster_count, (int) fat->cluster_bytes);

    *out = fat;
    return 0;
}

/*
* Only valid before fat_start_driver (there is no way to stop the thread).
*/
void fat_destroy(struct fat* fat) {
    FreeHeap(fat->table);
    FreeHeap(fat);
}

/*
* ---------------------------------------------------------------------------
* The FAT
* ---------------------------------------------------------------------------
*/

/*
* Reads the raw entry for a cluster. The cluster must be at most max_cluster,
* which is checked when mounting to be inside the table.
*/
static uint32_t fat_get_entry(const struct fat* fat, uint32_t cluster) {
    if (fat->type == FAT_TYPE_12) {
        /*
        * Entries are 12 bits, so two of them are packed into 3 bytes. Entry N
        * starts at byte N * 1.5. For odd N that means the high 12 bits of the
        * 16 bits read there.
        */
        uint32_t offset = cluster + cluster / 2;
        uint32_t value = fat->table[offset] | ((uint32_t) fat->table[offset + 1] << 8);
        return (cluster & 1) ? (value >> 4) : (value & 0xFFF);
    }

    return Le16(fat->table + cluster * 2);
}

/*
* Finds the cluster after 'cluster' in its chain. Returns 0 and the next cluster,
* FAT_END if this is the last cluster, or EIO if the chain is corrupt (this
* includes free, reserved and bad clusters, and clusters that don't exist).
*/
static int fat_next_cluster(const struct fat* fat, uint32_t cluster, uint32_t* next) {
    if (cluster < 2 || cluster > fat->max_cluster) {
        return EIO;
    }

    uint32_t entry = fat_get_entry(fat, cluster);
    uint32_t end_marker = (fat->type == FAT_TYPE_12) ? 0xFF8 : 0xFFF8;

    if (entry >= end_marker) {
        return FAT_END;
    }

    /*
    * The bad cluster marker (0xFF7 / 0xFFF7) and the reserved values around
    * it are larger than max_cluster, so they are caught here too.
    */
    if (entry < 2 || entry > fat->max_cluster) {
        return EIO;
    }

    *next = entry;
    return 0;
}

/*
* The byte offset of the start of a (valid) cluster, from the start of the volume.
*/
static uint64_t fat_cluster_to_byte(const struct fat* fat, uint32_t cluster) {
    uint64_t sector = (uint64_t) fat->data_start_sector + (uint64_t) (cluster - 2) * fat->sectors_per_cluster;
    return sector * FAT_SECTOR_SIZE;
}

/*
* ---------------------------------------------------------------------------
* Reading file data
* ---------------------------------------------------------------------------
*/

/*
* Copies 'count' bytes, starting at a byte offset on the volume, to the transfer.
* count must not be more than the transfer has room for.
*
* Whole sectors go straight to the destination. A partial sector at either end
* has to be read into a buffer first, and then the part we want is copied out.
*/
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
static int fat_read_file_sync(struct fat* fat, uint32_t first_cluster, uint32_t file_size, struct transfer* io) {
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
    * Find the cluster that the offset is in.
    */
    uint64_t skip_clusters = (uint64_t) io->offset / fat->cluster_bytes;
    uint32_t within = (uint64_t) io->offset % fat->cluster_bytes;
    uint32_t cluster = first_cluster;

    for (uint64_t i = 0; i < skip_clusters; ++i) {
        int status = fat_next_cluster(fat, cluster, &cluster);
        if (status == FAT_END) {
            /* The file is shorter than its size says. */
            return EIO;
        } else if (status != 0) {
            return status;
        }
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
            int status = fat_next_cluster(fat, last, &next_cluster);
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
        int status = fat_read_linear(fat, fat_cluster_to_byte(fat, cluster) + within, count, io);
        if (status != 0) {
            return status;
        }

        size_left -= count;
        if (io->length_remaining == 0 || size_left == 0) {
            break;
        }

        if (chain_ended) {
            /* The file is shorter than its size says. */
            return EIO;
        }

        cluster = next_cluster;
        within = 0;
    }

    return 0;
}

/*
* ---------------------------------------------------------------------------
* Directories
* ---------------------------------------------------------------------------
*
* A directory is a list of 32 byte entries. The root directory is a fixed size
* area before the data clusters. Any other directory is stored in a chain of
* clusters like a file. An entry whose first byte is zero ends the directory,
* and one whose first byte is 0xE5 has been deleted.
*
* Long file names are stored in extra entries in front of the real one. We just
* skip over them.
*/

static char FatToUpper(char c) {
    return (c >= 'a' && c <= 'z') ? (char) (c - 'a' + 'A') : c;
}

static char FatToLower(char c) {
    return (c >= 'A' && c <= 'Z') ? (char) (c - 'A' + 'a') : c;
}

/*
* File names on FAT aren't case sensitive.
*/
static bool FatNameEquals(const char* a, const char* b) {
    while (*a != 0 && *b != 0) {
        if (FatToUpper(*a) != FatToUpper(*b)) {
            return false;
        }

        ++a;
        ++b;
    }

    return *a == *b;
}

/*
* Entries we should show: not deleted, not part of a long file name, and not the
* volume label. (Assumes the entry isn't the end marker.)
*/
static bool FatEntryVisible(const uint8_t* entry) {
    if (entry[0] == 0xE5) {
        return false;
    }

    if ((entry[11] & ATTR_LFN_MASK) == ATTR_LFN) {
        return false;
    }

    if (entry[11] & ATTR_VOLUME_ID) {
        return false;
    }

    return true;
}

/*
* Turns the space padded "NAME    EXT" in an entry into "name.ext". 'out' needs
* room for 13 characters. Windows NT and later say in a flags byte when the name
* or extension should be shown in lower case, so we honour that.
*/
static void FatEntryName(const uint8_t* entry, char* out) {
    bool lower_name = entry[12] & NT_LOWER_NAME;
    bool lower_ext = entry[12] & NT_LOWER_EXT;

    int name_length = 8;
    while (name_length > 0 && entry[name_length - 1] == ' ') {
        --name_length;
    }

    int ext_length = 3;
    while (ext_length > 0 && entry[8 + ext_length - 1] == ' ') {
        --ext_length;
    }

    int n = 0;
    for (int i = 0; i < name_length; ++i) {
        char c = (char) entry[i];

        /* 0x05 stands in for a real first byte of 0xE5, which would mean "deleted". */
        if (i == 0 && (uint8_t) c == 0x05) {
            c = (char) 0xE5;
        }

        out[n++] = lower_name ? FatToLower(c) : c;
    }

    if (ext_length > 0) {
        out[n++] = '.';

        for (int i = 0; i < ext_length; ++i) {
            char c = (char) entry[8 + i];
            out[n++] = lower_ext ? FatToLower(c) : c;
        }
    }

    out[n] = 0;
}

/*
* FAT has no inodes, so make something up that's unique per file and directory.
* Anything with data is identified by its first cluster. That also means "." and
* ".." entries agree with the directory they point to. Empty files have no
* cluster, so they use the position of their entry instead (in a range that
* can't clash with the cluster based numbers).
*/
static uint32_t fat_make_ino(uint32_t first_cluster, bool directory, uint32_t dir_cluster, uint32_t index) {
    if (directory && first_cluster == 0) {
        return FAT_ROOT_INO;
    }

    if (first_cluster != 0) {
        return first_cluster + 1;
    }

    return 0x20000 + ((dir_cluster << 16) | index);
}

static void fat_decode_entry(uint32_t dir_cluster, const uint8_t* entry, uint32_t index, struct fat_node_info* info) {
    info->directory = (entry[11] & ATTR_DIRECTORY) != 0;
    info->first_cluster = Le16(entry + 26);   /* The high half is only for FAT32. */
    info->size = info->directory ? 0 : Le32(entry + 28);
    info->ino = fat_make_ino(info->first_cluster, info->directory, dir_cluster, index);
}

/*
* Reads one sector's worth (16 entries) of a directory. Returns FAT_END if the
* directory doesn't have that many sectors.
*
* The cluster chain is walked from the start each time. It is only in memory, so
* that's cheap enough for the size of directories we expect.
*/
static int fat_dir_read_sector(struct fat* fat, uint32_t dir_cluster, uint32_t sector_index, uint8_t* buffer) {
    if (sector_index >= FAT_MAX_DIR_SECTORS) {
        return FAT_END;
    }

    uint64_t byte;

    if (dir_cluster == 0) {
        if (sector_index >= fat->root_dir_sectors) {
            return FAT_END;
        }

        byte = (uint64_t) (fat->root_dir_start_sector + sector_index) * FAT_SECTOR_SIZE;

    } else {
        if (dir_cluster < 2 || dir_cluster > fat->max_cluster) {
            return EIO;
        }

        uint32_t cluster = dir_cluster;
        uint32_t hops = sector_index / fat->sectors_per_cluster;

        for (uint32_t i = 0; i < hops; ++i) {
            int status = fat_next_cluster(fat, cluster, &cluster);
            if (status != 0) {
                return status;      /* Includes FAT_END: ran out of clusters. */
            }
        }

        byte = fat_cluster_to_byte(fat, cluster) + (uint64_t) (sector_index % fat->sectors_per_cluster) * FAT_SECTOR_SIZE;
    }

    return fat_disk_read(fat->disk, buffer, FAT_SECTOR_SIZE, byte);
}

/*
* Searches a directory. If name isn't NULL, finds the entry with that name.
* Otherwise, finds the nth entry that should be shown (counting from 0).
*
* Returns ENOENT if there is no such entry. On success, the raw entry is copied
* to entry_out and its position in the directory goes in index_out.
*/
static int fat_dir_scan(struct fat* fat, uint32_t dir_cluster, const char* name, uint32_t nth,
                        uint8_t* entry_out, uint32_t* index_out) {
    uint8_t buffer[FAT_SECTOR_SIZE];
    uint32_t seen = 0;

    for (uint32_t sector = 0; ; ++sector) {
        int status = fat_dir_read_sector(fat, dir_cluster, sector, buffer);
        if (status == FAT_END) {
            return ENOENT;
        } else if (status != 0) {
            return status;
        }

        for (uint32_t i = 0; i < FAT_ENTRIES_PER_SECTOR; ++i) {
            const uint8_t* entry = buffer + i * FAT_ENTRY_SIZE;

            /* No more entries. */
            if (entry[0] == 0) {
                return ENOENT;
            }

            if (!FatEntryVisible(entry)) {
                continue;
            }

            bool match;
            if (name != NULL) {
                char entry_name[13];
                FatEntryName(entry, entry_name);
                match = FatNameEquals(name, entry_name);
            } else {
                match = (seen == nth);
                ++seen;
            }

            if (match) {
                memcpy(entry_out, entry, FAT_ENTRY_SIZE);
                *index_out = sector * FAT_ENTRIES_PER_SECTOR + i;
                return 0;
            }
        }
    }
}

int fat_follow(struct fat* fat, uint32_t dir_cluster, const char* name, struct fat_node_info* out) {
    assert(fat);
    assert(name);
    assert(out);

    /*
    * Without long file names, nothing can be longer than "NAME.EXT".
    */
    if (strlen(name) > 12) {
        return ENOENT;
    }

    uint8_t entry[FAT_ENTRY_SIZE];
    uint32_t index;

    int status = fat_dir_scan(fat, dir_cluster, name, 0, entry, &index);
    if (status != 0) {
        return status;
    }

    fat_decode_entry(dir_cluster, entry, index, out);
    return 0;
}

/*
* Does the actual work of reading a directory entry into the transfer. Runs on
* the filesystem thread.
*/
static int fat_read_directory_entry_sync(struct fat* fat, uint32_t dir_cluster, struct transfer* io) {
    struct dirent dir;
    memset(&dir, 0, sizeof(dir));

    if (io->offset % sizeof(struct dirent) != 0) {
        return EINVAL;
    }

    uint64_t entry_number = io->offset / sizeof(struct dirent);

    /*
    * Other directories have "." and ".." entries on disk, but the root
    * directory doesn't, so provide the "." ourselves.
    */
    if (dir_cluster == 0) {
        if (entry_number == 0) {
            strcpy(dir.d_name, ".");
            dir.d_namlen = 1;
            dir.d_ino = FAT_ROOT_INO;
            dir.d_disk = (size_t) fat->disk->node->stat.st_dev;
            dir.d_type = DT_DIR;
            return PerformTransfer(&dir, io, sizeof(struct dirent));
        }

        --entry_number;
    }

    uint8_t entry[FAT_ENTRY_SIZE];
    uint32_t index;

    int status = fat_dir_scan(fat, dir_cluster, NULL, (uint32_t) entry_number, entry, &index);
    if (status == ENOENT) {
        /* Gone past the end of the directory, so there is nothing to transfer. */
        return 0;
    } else if (status != 0) {
        return status;
    }

    struct fat_node_info info;
    fat_decode_entry(dir_cluster, entry, index, &info);

    char name[13];
    FatEntryName(entry, name);

    strcpy(dir.d_name, name);
    dir.d_namlen = strlen(name);
    dir.d_ino = info.ino;
    dir.d_disk = (size_t) fat->disk->node->stat.st_dev;
    dir.d_type = info.directory ? DT_DIR : DT_REG;

    /* Perform the transfer to the correct location */
    return PerformTransfer(&dir, io, sizeof(struct dirent));
}

/*
* ---------------------------------------------------------------------------
* The filesystem thread
* ---------------------------------------------------------------------------
*/

/*
* What gets posted to the message box. The message box copies it, so it can live
* on the poster's stack.
*/
struct fat_request {
    bool is_directory;
    uint32_t cluster;       /* The file's or directory's first cluster. */
    uint32_t file_size;     /* Files only. */
    struct aio* aio;
};

static void FatDriverThread(void* _fat) {
    BeginNewThread();

    struct fat* fat = _fat;

    while (true) {
        struct fat_request req;
        if (KeGetMessage(fat->msgbox, &req, TIMEOUT_INFINITE) != 0) {
            continue;
        }

        int status;
        if (req.is_directory) {
            status = fat_read_directory_entry_sync(fat, req.cluster, &req.aio->transfer);
        } else {
            status = fat_read_file_sync(fat, req.cluster, req.file_size, &req.aio->transfer);
        }

        /* The aio may be freed by the waiter as soon as this is called. */
        SignalDoneIO(req.aio, status);
    }
}

/*
* Creates the message box and thread for a mounted volume. Call this once, when
* the volume is mounted, before any reads can happen. There is nothing to undo
* it, so do it last.
*/
int fat_start_driver(struct fat* fat) {
    fat->msgbox = CreateMessageBox(sizeof(struct fat_request), FAT_QUEUE_DEPTH);
    if (fat->msgbox == NULL) {
        return ENOMEM;
    }

    CreateThreadEx(GetKernelVas(), FatDriverThread, fat, 1024 * 3);
    return 0;
}

/*
* Reads from a file into the transfer in aio.
*
* This only queues the request. Returns an error if it could not be queued
* (nothing is signalled in that case). Otherwise returns 0 and the read completes
* when the filesystem thread has finished, which the caller finds out about with
* WaitIO.
*/
int fat_read_file(struct fat* fat, uint32_t first_cluster, uint32_t file_size, struct aio* aio) {
    struct fat_request req = {
        .is_directory = false,
        .cluster = first_cluster,
        .file_size = file_size,
        .aio = aio,
    };

    return KePostMessage(fat->msgbox, (const void*) &req, TIMEOUT_INSTANT);
}

/*
* As above, for reading the next directory entry into the transfer.
*/
int fat_read_directory_entry(struct fat* fat, uint32_t dir_cluster, struct aio* aio) {
    struct fat_request req = {
        .is_directory = true,
        .cluster = dir_cluster,
        .file_size = 0,
        .aio = aio,
    };

    return KePostMessage(fat->msgbox, (const void*) &req, TIMEOUT_INSTANT);
}
