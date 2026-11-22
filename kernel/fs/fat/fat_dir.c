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
* skip over them (and so a new file can only be created with a name that fits in
* "8.3").
*/

#include <common.h>
#include <errno.h>
#include <vfs.h>
#include <string.h>
#include <assert.h>
#include <transfer.h>
#include <mutex.h>
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
*
* A directory is identified by its first cluster, which means that "." and ".."
* entries agree with the directory they point to.
*
* A file is identified by the position of its directory entry. (Not its first
* cluster, as that would change when a file that was empty has some data written
* to it, and a file must keep its number while it's open.) Those have the top
* bit set, which the cluster based numbers (at most 28 bits) never do.
*
* 'position' is the entry's position on the volume, in units of directory
* entries (so the sector number * 16 + the index within the sector).
*/
static uint32_t FatMakeIno(const struct fat* fat, uint32_t first_cluster, bool directory, uint64_t position) {
    if (directory) {
        if (first_cluster == fat->root_cluster) {
            return FAT_ROOT_INO;
        }

        return first_cluster + 1;
    }

    return 0x80000000u | ((uint32_t) position & 0x7FFFFFFFu);
}

static void FatDecodeEntry(const struct fat* fat, const uint8_t* entry, uint64_t position, struct fat_node_info* info) {
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
    info->ino = FatMakeIno(fat, info->first_cluster, info->directory, position);
    info->entry_byte = position * FAT_ENTRY_SIZE;
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

static int FatDirStart(struct fat* fat, uint32_t dir_cluster, struct fat_dir_cursor* cursor) {
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
static int FatDirNextSector(struct fat_dir_cursor* cursor, uint8_t* buffer, uint64_t* byte_out) {
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
            int status = FatNextCluster(fat, cursor->cluster, &next);
            if (status != 0) {
                return status;      /* Includes FAT_END: ran out of clusters. */
            }

            cursor->cluster = next;
            cursor->sector_in_cluster = 0;
            cursor->advance = false;
        }

        byte = FatClusterToByte(fat, cursor->cluster) + (uint64_t) cursor->sector_in_cluster * FAT_SECTOR_SIZE;
    }

    int status = FatDiskRead(fat->disk, buffer, FAT_SECTOR_SIZE, byte);
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
* to entry_out and its position on the volume (see FatMakeIno) goes in
* position_out.
*/
static int FatDirScan(struct fat* fat, uint32_t dir_cluster, const char* name, uint32_t nth,
                        uint8_t* entry_out, uint64_t* position_out) {
    struct fat_dir_cursor cursor;
    int status = FatDirStart(fat, dir_cluster, &cursor);
    if (status != 0) {
        return status;
    }

    uint8_t buffer[FAT_SECTOR_SIZE];
    uint32_t seen = 0;

    while (true) {
        uint64_t sector_byte;
        status = FatDirNextSector(&cursor, buffer, &sector_byte);
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

int FatFollow(struct fat* fat, uint32_t dir_cluster, const char* name, struct fat_node_info* out) {
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

    int status = FatDirScan(fat, dir_cluster, name, 0, entry, &position);
    if (status != 0) {
        return status;
    }

    FatDecodeEntry(fat, entry, position, out);
    return 0;
}

/*
* Does the actual work of reading a directory entry into the transfer. Runs on
* the filesystem thread.
*/
int FatReadDirectoryEntrySync(struct fat* fat, uint32_t dir_cluster, struct transfer* io) {
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

    int status = FatDirScan(fat, dir_cluster, NULL, (uint32_t) entry_number, entry, &position);
    if (status == ENOENT) {
        /* Gone past the end of the directory, so there is nothing to transfer. */
        return 0;
    } else if (status != 0) {
        return status;
    }

    struct fat_node_info info;
    FatDecodeEntry(fat, entry, position, &info);

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
* Changing directories
* ---------------------------------------------------------------------------
*
* Everything here needs write_lock.
*/

/*
* Replaces the directory entry that is at the given place on the volume.
*/
static int FatPutEntry(struct fat* fat, uint64_t entry_byte, const uint8_t* entry) {
    uint8_t sector[FAT_SECTOR_SIZE];
    uint64_t sector_byte = entry_byte - entry_byte % FAT_SECTOR_SIZE;

    int status = FatDiskRead(fat->disk, sector, FAT_SECTOR_SIZE, sector_byte);
    if (status != 0) {
        return status;
    }

    memcpy(sector + entry_byte % FAT_SECTOR_SIZE, entry, FAT_ENTRY_SIZE);

    return FatDiskWrite(fat->disk, sector, FAT_SECTOR_SIZE, sector_byte);
}

/*
* Writes a file's first cluster and size to its directory entry. Done after
* either has changed.
*
* (The modification time isn't updated, as there's nothing to get the time from.)
*/
int FatUpdateEntry(struct fat* fat, const struct fat_file* file) {
    uint8_t sector[FAT_SECTOR_SIZE];
    uint64_t sector_byte = file->entry_byte - file->entry_byte % FAT_SECTOR_SIZE;
    uint8_t* entry = sector + file->entry_byte % FAT_SECTOR_SIZE;

    int status = FatDiskRead(fat->disk, sector, FAT_SECTOR_SIZE, sector_byte);
    if (status != 0) {
        return status;
    }

    /* It should still be the entry of a file, as nothing ever moves or removes one. */
    if (entry[0] == 0 || entry[0] == 0xE5 || (entry[11] & (ATTR_DIRECTORY | ATTR_VOLUME_ID)) != 0) {
        return EIO;
    }

    FatPut16(entry + 26, file->first_cluster & 0xFFFF);
    if (fat->type == FAT_TYPE_32) {
        FatPut16(entry + 20, file->first_cluster >> 16);
    }

    FatPut32(entry + 28, file->size);
    entry[11] |= ATTR_ARCHIVE;

    return FatDiskWrite(fat->disk, sector, FAT_SECTOR_SIZE, sector_byte);
}

/*
* Characters that can't be in a short name. (As well as the dot, which has a
* special meaning there, and the lower case letters, which are made upper case.)
*/
static bool FatNameCharValid(uint8_t c) {
    if (c <= ' ' || c == 0x7F) {
        return false;
    }

    switch (c) {
    case '"': case '*': case '+': case ',': case '/': case ':': case ';':
    case '<': case '=': case '>': case '?': case '[': case '\\': case ']': case '|':
        return false;
    }

    return true;
}

/*
* Copies one half of a name (the name or the extension) into its place in the
* padded form, as upper case. Notes whether any lower and upper case letters
* were seen. Returns false if there's a character that isn't allowed.
*/
static bool FatCopyNamePart(const char* source, size_t length, uint8_t* dest, bool* has_lower, bool* has_upper) {
    for (size_t i = 0; i < length; ++i) {
        uint8_t c = (uint8_t) source[i];
        if (!FatNameCharValid(c)) {
            return false;
        }

        if (c >= 'a' && c <= 'z') {
            *has_lower = true;
            c = (uint8_t) (c - 'a' + 'A');
        } else if (c >= 'A' && c <= 'Z') {
            *has_upper = true;
        }

        dest[i] = c;
    }

    return true;
}

/*
* Turns a name into the space padded "NAME    EXT" form that's stored in an
* entry (the opposite of FatEntryName), and the Windows NT flags that say which
* parts to show in lower case. Returns EINVAL if the name can't be stored in an
* entry: more than 8 characters before the dot or 3 after, more than one dot,
* nothing before or after the dot, or a character that isn't allowed.
*
* A name or extension that is all lower case is shown as such, as is one that is
* all upper case. Mixed case is stored as upper case (as names aren't case
* sensitive, it can still be found with any case).
*/
static int FatMakeShortName(const char* name, uint8_t* padded, uint8_t* nt_flags) {
    const char* dot = NULL;
    for (const char* p = name; *p != 0; ++p) {
        if (*p == '.') {
            if (dot != NULL) {
                return EINVAL;
            }
            dot = p;
        }
    }

    size_t name_length = (dot != NULL) ? (size_t) (dot - name) : strlen(name);
    size_t ext_length = (dot != NULL) ? strlen(dot + 1) : 0;

    if (name_length == 0 || name_length > 8 || ext_length > 3 || (dot != NULL && ext_length == 0)) {
        return EINVAL;
    }

    memset(padded, ' ', 11);

    bool name_lower = false, name_upper = false;
    bool ext_lower = false, ext_upper = false;

    if (!FatCopyNamePart(name, name_length, padded, &name_lower, &name_upper)) {
        return EINVAL;
    }

    if (ext_length != 0 && !FatCopyNamePart(dot + 1, ext_length, padded + 8, &ext_lower, &ext_upper)) {
        return EINVAL;
    }

    /* The reverse of what FatEntryName does. */
    if (padded[0] == 0xE5) {
        padded[0] = 0x05;
    }

    *nt_flags = 0;
    if (name_lower && !name_upper) {
        *nt_flags |= NT_LOWER_NAME;
    }
    if (ext_lower && !ext_upper) {
        *nt_flags |= NT_LOWER_EXT;
    }

    return 0;
}

/*
* Makes a directory longer by one (cleared) cluster. The place of the first entry
* in the new cluster is returned.
*/
static int FatDirGrow(struct fat* fat, uint32_t last_cluster, uint64_t* entry_byte_out) {
    uint32_t fresh;
    int status = FatAllocCluster(fat, last_cluster + 1, &fresh);
    if (status != 0) {
        return status;
    }

    /*
    * It has to be cleared first (an entry starting with zero ends the directory,
    * and the cluster has old data in it), and only then added to the directory.
    */
    uint64_t byte = FatClusterToByte(fat, fresh);

    status = FatDiskZero(fat->disk, byte, fat->cluster_bytes);
    if (status == 0) {
        status = FatSetNext(fat, last_cluster, fresh);
    }

    if (status != 0) {
        FatFreeCluster(fat, fresh);
        return status;
    }

    *entry_byte_out = byte;
    return 0;
}

/*
* Finds a place in a directory for a new entry: a deleted one, or the one that
* ends the directory. (Making the directory longer if it's full.) Returns the
* place of it on the volume, or ENOSPC if the directory can't get any bigger.
*/
static int FatDirFindFree(struct fat* fat, uint32_t dir_cluster, uint64_t* entry_byte_out) {
    struct fat_dir_cursor cursor;
    int status = FatDirStart(fat, dir_cluster, &cursor);
    if (status != 0) {
        return status;
    }

    uint8_t buffer[FAT_SECTOR_SIZE];

    while (true) {
        uint64_t sector_byte;
        status = FatDirNextSector(&cursor, buffer, &sector_byte);
        if (status == FAT_END) {
            break;
        } else if (status != 0) {
            return status;
        }

        for (uint32_t i = 0; i < FAT_ENTRIES_PER_SECTOR; ++i) {
            uint8_t first = buffer[i * FAT_ENTRY_SIZE];
            if (first == 0 || first == 0xE5) {
                *entry_byte_out = sector_byte + (uint64_t) i * FAT_ENTRY_SIZE;
                return 0;
            }
        }
    }

    /*
    * Full. The FAT12/FAT16 root directory can't be made bigger, and no other
    * directory can have more than FAT_MAX_DIR_SECTORS sectors.
    */
    if (cursor.fixed_root || cursor.sector >= FAT_MAX_DIR_SECTORS) {
        return ENOSPC;
    }

    /* Otherwise the cursor stopped at the end of the chain, so this is its last cluster. */
    return FatDirGrow(fat, cursor.cluster, entry_byte_out);
}

static int FatCreateLocked(struct fat* fat, uint32_t dir_cluster, const char* name, const uint8_t* padded,
                             uint8_t nt_flags, struct fat_node_info* out) {
    /*
    * Whoever called us has probably just looked for it, but that was before we
    * had the lock.
    */
    uint8_t existing[FAT_ENTRY_SIZE];
    uint64_t existing_position;

    int status = FatDirScan(fat, dir_cluster, name, 0, existing, &existing_position);
    if (status == 0) {
        return EEXIST;
    } else if (status != ENOENT) {
        return status;
    }

    uint64_t entry_byte;
    status = FatDirFindFree(fat, dir_cluster, &entry_byte);
    if (status != 0) {
        return status;
    }

    /*
    * An empty file: no clusters (they come with the first write) and no size.
    */
    uint8_t entry[FAT_ENTRY_SIZE];
    memset(entry, 0, sizeof(entry));
    memcpy(entry, padded, 11);
    entry[11] = ATTR_ARCHIVE;
    entry[12] = nt_flags;
    FatPut16(entry + 14, FAT_DEFAULT_TIME);     /* Created. */
    FatPut16(entry + 16, FAT_DEFAULT_DATE);
    FatPut16(entry + 18, FAT_DEFAULT_DATE);     /* Last accessed. */
    FatPut16(entry + 22, FAT_DEFAULT_TIME);     /* Last modified. */
    FatPut16(entry + 24, FAT_DEFAULT_DATE);

    status = FatPutEntry(fat, entry_byte, entry);
    if (status != 0) {
        return status;
    }

    out->first_cluster = 0;
    out->size = 0;
    out->directory = false;
    out->ino = FatMakeIno(fat, 0, false, entry_byte / FAT_ENTRY_SIZE);
    out->entry_byte = entry_byte;
    return 0;
}

/*
* Creates an empty file in a directory. Returns EEXIST if there's already
* something with that name (in any case) and EINVAL if the name is no good (see
* FatMakeShortName), and ENOSPC if the directory has no room for it.
*/
int FatCreate(struct fat* fat, uint32_t dir_cluster, const char* name, struct fat_node_info* out) {
    assert(fat);
    assert(name);
    assert(out);

    uint8_t padded[11];
    uint8_t nt_flags;

    int status = FatMakeShortName(name, padded, &nt_flags);
    if (status != 0) {
        return status;
    }

    AcquireMutex(fat->write_lock, TIMEOUT_INFINITE);
    status = FatCreateLocked(fat, dir_cluster, name, padded, nt_flags, out);
    ReleaseMutex(fat->write_lock);

    return status;
}
