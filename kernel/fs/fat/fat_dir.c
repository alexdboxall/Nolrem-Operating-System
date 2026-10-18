/*
* fat_dir.c - directories
*
* A directory is a list of 32 byte entries. On FAT12 and FAT16 the root directory
* is a fixed size area before the data clusters. Any other directory (and the
* FAT32 root directory) is stored in a chain of clusters like a file. An entry
* whose first byte is zero ends the directory, and one whose first byte is 0xE5
* has been deleted.
*
* Long file names are stored in extra entries in front of the real one. We just
* skip over them.
*/

#include <common.h>
#include <errno.h>
#include <vfs.h>
#include <string.h>
#include <assert.h>
#include <transfer.h>
#include <sys/types.h>
#include <dirent.h>
#include <fs/fat/fat_private.h>

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
* cluster, so they use the position of their directory entry instead. Those have
* the top bit set, which the cluster based numbers (at most 28 bits) never do.
*
* 'position' is the entry's position on the volume, in units of directory
* entries (so the sector number * 16 + the index within the sector).
*/
static uint32_t fat_make_ino(const struct fat* fat, uint32_t first_cluster, bool directory, uint64_t position) {
    if (directory && first_cluster == fat->root_cluster) {
        return FAT_ROOT_INO;
    }

    if (first_cluster != 0) {
        return first_cluster + 1;
    }

    return 0x80000000u | ((uint32_t) position & 0x7FFFFFFFu);
}

static void fat_decode_entry(const struct fat* fat, const uint8_t* entry, uint64_t position, struct fat_node_info* info) {
    info->directory = (entry[11] & ATTR_DIRECTORY) != 0;

    info->first_cluster = Le16(entry + 26);
    if (fat->type == FAT_TYPE_32) {
        /* The high half is only valid on FAT32. */
        info->first_cluster |= (uint32_t) Le16(entry + 20) << 16;
    }

    /*
    * A directory that says it starts at cluster 0 is the root directory. That's
    * how ".." entries refer to it (even on FAT32, where the root directory isn't
    * at cluster 0).
    */
    if (info->directory && info->first_cluster == 0) {
        info->first_cluster = fat->root_cluster;
    }

    info->size = info->directory ? 0 : Le32(entry + 28);
    info->ino = fat_make_ino(fat, info->first_cluster, info->directory, position);
}

/*
* ---------------------------------------------------------------------------
* Walking through a directory sector by sector
* ---------------------------------------------------------------------------
*
* Directory clusters are followed one after the other. As the FAT is not in
* memory, finding the nth cluster by starting from the beginning each time would
* be a lot of work for big directories.
*/

struct fat_dir_cursor {
    struct fat* fat;
    bool fixed_root;            /* The FAT12/FAT16 root directory. */
    uint32_t sector;            /* The next sector to read, counting from the start of the directory. */
    uint32_t cluster;           /* Cluster that holds the next sector. (Not for the fixed root.) */
    uint32_t sector_in_cluster; /* Where in that cluster it is. */
    bool advance;               /* The cluster is used up and we must follow the chain to the next one. */
};

static int fat_dir_start(struct fat* fat, uint32_t dir_cluster, struct fat_dir_cursor* cursor) {
    cursor->fat = fat;
    cursor->sector = 0;
    cursor->sector_in_cluster = 0;
    cursor->advance = false;
    cursor->fixed_root = (dir_cluster == 0);
    cursor->cluster = dir_cluster;

    if (!cursor->fixed_root && (dir_cluster < 2 || dir_cluster > fat->max_cluster)) {
        return EIO;
    }

    return 0;
}

/*
* Reads the next sector (16 entries) of the directory. Returns FAT_END if the
* directory has no more sectors. byte_out is where the sector is on the volume.
*/
static int fat_dir_next_sector(struct fat_dir_cursor* cursor, uint8_t* buffer, uint64_t* byte_out) {
    struct fat* fat = cursor->fat;

    if (cursor->sector >= FAT_MAX_DIR_SECTORS) {
        return FAT_END;
    }

    uint64_t byte;

    if (cursor->fixed_root) {
        if (cursor->sector >= fat->root_dir_sectors) {
            return FAT_END;
        }

        byte = (uint64_t) (fat->root_dir_start_sector + cursor->sector) * FAT_SECTOR_SIZE;

    } else {
        if (cursor->advance) {
            uint32_t next;
            int status = fat_next_cluster(fat, cursor->cluster, &next);
            if (status != 0) {
                return status;      /* Includes FAT_END: ran out of clusters. */
            }

            cursor->cluster = next;
            cursor->sector_in_cluster = 0;
            cursor->advance = false;
        }

        byte = fat_cluster_to_byte(fat, cursor->cluster) + (uint64_t) cursor->sector_in_cluster * FAT_SECTOR_SIZE;
    }

    int status = fat_disk_read(fat->disk, buffer, FAT_SECTOR_SIZE, byte);
    if (status != 0) {
        return status;
    }

    ++cursor->sector;

    if (!cursor->fixed_root) {
        ++cursor->sector_in_cluster;
        if (cursor->sector_in_cluster == fat->sectors_per_cluster) {
            cursor->advance = true;
        }
    }

    *byte_out = byte;
    return 0;
}

/*
* Searches a directory. If name isn't NULL, finds the entry with that name.
* Otherwise, finds the nth entry that should be shown (counting from 0).
*
* Returns ENOENT if there is no such entry. On success, the raw entry is copied
* to entry_out and its position on the volume (see fat_make_ino) goes in
* position_out.
*/
static int fat_dir_scan(struct fat* fat, uint32_t dir_cluster, const char* name, uint32_t nth,
                        uint8_t* entry_out, uint64_t* position_out) {
    struct fat_dir_cursor cursor;
    int status = fat_dir_start(fat, dir_cluster, &cursor);
    if (status != 0) {
        return status;
    }

    uint8_t buffer[FAT_SECTOR_SIZE];
    uint32_t seen = 0;

    while (true) {
        uint64_t sector_byte;
        status = fat_dir_next_sector(&cursor, buffer, &sector_byte);
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
                *position_out = (sector_byte / FAT_SECTOR_SIZE) * FAT_ENTRIES_PER_SECTOR + i;
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
    * Without long file names, nothing can be longer than "8.3".
    */
    if (strlen(name) > 12) {
        return ENOENT;
    }

    uint8_t entry[FAT_ENTRY_SIZE];
    uint64_t position;

    int status = fat_dir_scan(fat, dir_cluster, name, 0, entry, &position);
    if (status != 0) {
        return status;
    }

    fat_decode_entry(fat, entry, position, out);
    return 0;
}

/*
* Does the actual work of reading a directory entry into the transfer. Runs on
* the filesystem thread.
*/
int fat_read_directory_entry_sync(struct fat* fat, uint32_t dir_cluster, struct transfer* io) {
    struct dirent dir;
    memset(&dir, 0, sizeof(dir));

    if (io->offset % sizeof(struct dirent) != 0) {
        return EINVAL;
    }

    uint64_t entry_number = io->offset / sizeof(struct dirent);

    /*
    * Other directories have "." and ".." entries on disk, but the root
    * directory doesn't (on any type of FAT), so provide the "." ourselves.
    */
    if (dir_cluster == fat->root_cluster) {
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
    uint64_t position;

    int status = fat_dir_scan(fat, dir_cluster, NULL, (uint32_t) entry_number, entry, &position);
    if (status == ENOENT) {
        /* Gone past the end of the directory, so there is nothing to transfer. */
        return 0;
    } else if (status != 0) {
        return status;
    }

    struct fat_node_info info;
    fat_decode_entry(fat, entry, position, &info);

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
