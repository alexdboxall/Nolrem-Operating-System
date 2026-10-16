#include <common.h>
#include <errno.h>
#include <vfs.h>
#include <string.h>
#include <assert.h>
#include <panic.h>
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
#include <fs/demofs/demofs_private.h>

#define SECTOR_SIZE 512

/*
* How many requests can be queued for the filesystem thread at once.
*/
#define DEMOFS_QUEUE_DEPTH 16

/*
* ---------------------------------------------------------------------------
* Overview
*
* Like the floppy driver, each demofs mount has its own thread and message box.
* The vnode read operations (demofs_read_file and demofs_read_directory_entry)
* only post a request to the message box and return, so the request starts
* being processed straight away on the filesystem thread. When the thread has
* finished it calls SignalDoneIO, and whoever calls WaitIO gets the result.
* That might happen before they call WaitIO, or they might have to wait.
*
* The filesystem thread is entirely synchronous: it reads from the disk with
* CreateIO + ReadFile + WaitIO and just waits for each read in turn. There is no
* chaining of sub-requests or completion callbacks to worry about.
*
* Follow is still synchronous too, and runs on the caller's thread. That's fine
* as nothing in struct demofs is modified after mount: every function here uses
* only its own local buffers, so the thread and Follow can run at the same time.
* ---------------------------------------------------------------------------
*/

/*
* Tear down an aio that was created with CreateIO but which the driver failed to
* start (so it will never be signalled). Signalling it ourselves lets WaitIO
* drop the reference to the file and destroy the mutex.
*/
static void demofs_io_abort(struct aio* aio, int status) {
    aio->callback = NULL;
    aio->sub = NULL;
    SignalDoneIO(aio, status);
    WaitIO(aio, TIMEOUT_INFINITE);
}

/*
* Synchronous read from the disk. On return, io has been advanced by however much
* was transferred (like the aio's transfer is by the driver).
*/
static int demofs_read_disk_transfer(struct demofs* fs, struct transfer* io) {
    struct aio aio;
    CreateIO(&aio, fs->disk, *io, NULL, NULL);

    int status = ReadFile(&aio);
    if (status != 0) {
        demofs_io_abort(&aio, status);
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
static int demofs_read_disk_sync(struct demofs* fs, void* buffer, size_t length, uint64_t offset) {
    struct transfer io = CreateKernelTransfer(buffer, length, offset, TRANSFER_READ);

    int status = demofs_read_disk_transfer(fs, &io);
    if (status == 0 && io.length_remaining != 0) {
        /* A short read means something is wrong with the disk. */
        status = EIO;
    }

    return status;
}

/*
* We are going to use the high bit of an inode ID to indicate whether or not
* we are talking about a directory or not (high bit set = directory).
*
* This allows us to, for example, easily catch ENOTDIR in demofs_follow.
*
* Remember to use INODE_TO_SECTOR. Note that inodes are only stored using 24 bits
* anyway. 
*/
int demofs_read_inode(struct demofs* fs, ino_t inode, uint8_t* buffer) {
    return demofs_read_disk_sync(fs, buffer, SECTOR_SIZE, (uint64_t) SECTOR_SIZE * INODE_TO_SECTOR(inode));
}

/*
* Does the actual work of reading from a file into the transfer. 'file' is the
* file's first sector and 'file_size' is the total length of the file.
* Runs on the filesystem thread.
*/
static int demofs_read_file_sync(struct demofs* fs, ino_t file, uint32_t file_size, struct transfer* io) {
    if ((uint64_t) io->offset >= file_size) {
        /* End of file, nothing to transfer. */
        return 0;
    }

    uint32_t file_size_left = file_size - io->offset;

    while (io->length_remaining != 0 && file_size_left != 0) {
        uint64_t sector = (uint64_t) file + (uint64_t) io->offset / SECTOR_SIZE;
        uint64_t sector_offset = (uint64_t) io->offset % SECTOR_SIZE;

        if (sector_offset == 0 && io->length_remaining >= SECTOR_SIZE && file_size_left >= SECTOR_SIZE) {
            /*
            * We have an aligned sector amount, so transfer it all directly,
            * except for possibly a few bytes at the end.
            *
            * ReadFile only allows lengths that are a multiple of the sector
            * size, so round down to the nearest sector. Also never read past the
            * end of the file. The leftover is picked up by a later iteration.
            *
            * We need the disk offset, not the file offset, so the disk transfer
            * is a copy of ours with the offset replaced. Ours is advanced by
            * however much the disk actually transferred.
            */
            uint64_t length = MIN((uint64_t) io->length_remaining, (uint64_t) file_size_left);
            length -= length % SECTOR_SIZE;

            struct transfer disk_io = *io;
            disk_io.offset = sector * SECTOR_SIZE;
            disk_io.length_remaining = length;

            int status = demofs_read_disk_transfer(fs, &disk_io);
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
            file_size_left -= transferred;

        } else {
            /*
            * A partial sector transfer.
            *
            * We must read the sector into an internal buffer, and then copy a
            * subsection of that to the return buffer.
            */
            uint8_t sector_buffer[SECTOR_SIZE];

            /* Read the sector */
            int status = demofs_read_disk_sync(fs, sector_buffer, SECTOR_SIZE, sector * SECTOR_SIZE);
            if (status != 0) {
                return status;
            }

            /* Transfer to the correct buffer (this advances our transfer) */
            size_t transfer_size = MIN(
                MIN((uint64_t) SECTOR_SIZE - sector_offset, (uint64_t) io->length_remaining),
                (uint64_t) file_size_left
            );

            status = PerformTransfer(sector_buffer + sector_offset, io, transfer_size);
            if (status != 0) {
                return status;
            }

            file_size_left -= transfer_size;
        }
    }
    
    return 0;
}

int demofs_follow(struct demofs* fs, ino_t parent, ino_t* child, const char* name, uint32_t* file_length_out) {
    assert(fs);
    assert(fs->disk);
    assert(child);
    assert(name);
    assert(file_length_out);
    assert(SECTOR_SIZE % 32 == 0);

    uint8_t buffer[SECTOR_SIZE];

    if (strlen(name) > MAX_NAME_LENGTH) {
        return ENAMETOOLONG;
    }

    if (!INODE_IS_DIR(parent)) {
        return ENOTDIR;
    }

    /*
    * The directory may contain many entries, so we need to iterate through them.
    */
    while (true) {
        /*
        * Grab the current entry.
        */
        int status = demofs_read_inode(fs, parent, buffer);
        if (status != 0) {
            return status;
        }

        /*
        * Something went very wrong if the directory header is not present!
        */
        if (buffer[0] != 0xFF && buffer[0] != 0xFE) {
            return EIO;
        }

        for (int i = 1; i < SECTOR_SIZE / 32; ++i) {
            /*
            * Check if there are no more names in the directory.
            */
            if (buffer[i * 32] == 0) {
                return ENOENT;
            }

            /*
            * Check if we've found the name.
            */
            if (!strncmp(name, (char*) buffer + i * 32, MAX_NAME_LENGTH)) {
                /*
                * If so, read the inode number and return it.
                * Remember to add the directory flag if necessary.
                */
                ino_t inode = buffer[i * 32 + MAX_NAME_LENGTH + 4];
                inode |= (ino_t) buffer[i * 32 + MAX_NAME_LENGTH + 5] << 8;
                inode |= (ino_t) buffer[i * 32 + MAX_NAME_LENGTH + 6] << 16;

                if (buffer[i * 32 + MAX_NAME_LENGTH + 7] & 1) {
                    /*
                    * This is a directory.
                    */
                    inode = INODE_TO_DIR(inode);
                    *file_length_out = 0;

                } else {
                    /*
                    * This is a file.
                    */
                    uint32_t length = buffer[i * 32 + MAX_NAME_LENGTH];
                    length |= (uint32_t) buffer[i * 32 + MAX_NAME_LENGTH + 1] << 8;
                    length |= (uint32_t) buffer[i * 32 + MAX_NAME_LENGTH + 2] << 16;
                    length |= (uint32_t) buffer[i * 32 + MAX_NAME_LENGTH + 3] << 24;

                    *file_length_out = length;
                }

                *child = inode;
                return 0;
            }
        }

        /*
        * Now we need to move on to the next entry if there is one.
        */
        if (buffer[0] == 0xFF) {
            /* No more entries. */
            return ENOENT;

        } else if (buffer[0] == 0xFE) {
            /*
            * There is another entry, so read its inode and keep the loop going
            */
            parent = buffer[1];
            parent |= (ino_t) buffer[2] << 8;
            parent |= (ino_t) buffer[3] << 16;

            /*
            * Add the directory bit to the inode number as it should be a directory.
            */
            parent = INODE_TO_DIR(parent);

        } else {
            /*
            * Something went very wrong if the directory header is not present!
            */
            return EIO;
        }
    }
}

/*
* Does the actual work of reading a directory entry into the transfer.
* Runs on the filesystem thread.
*/
static int demofs_read_directory_entry_sync(struct demofs* fs, ino_t directory, struct transfer* io) {
    if (!INODE_IS_DIR(directory)) {
        return ENOTDIR;
    }

    assert(SECTOR_SIZE % 32 == 0);
    uint8_t buffer[SECTOR_SIZE];

    struct dirent dir;
    
    if (io->offset % sizeof(struct dirent) != 0) {
        return EINVAL;
    }

    int entry_number = io->offset / sizeof(struct dirent);

    /*
     * Hack the current directory (".") into it.
     */
    if (entry_number == 0) {
        strcpy(dir.d_name, ".");
        dir.d_namlen = 1;
        dir.d_ino = directory & 0x7FFFFFFF;
        dir.d_disk = (size_t) fs->disk->node->stat.st_dev;
        dir.d_type = DT_DIR;
        return PerformTransfer(&dir, io, sizeof(struct dirent));

    } else {
        --entry_number;
    }

    /*
    * Each directory inode contains 15 files, and a pointer to the next directory entry.
    * Add 1 to the offset to skip past the header.
    */
    int indirections = entry_number / 15;
    int offset = entry_number % 15 + 1;

    /*
    * Get the correct inode 
    */
    int status = 0;
    ino_t current_inode = directory;
    for (int i = 0; i < indirections; ++i) {
        status = demofs_read_inode(fs, current_inode, buffer);
        if (status != 0) {
            return status;
        }

        if (i == indirections - 1) {
            if (buffer[0] != 0xFF) {
                return EIO;
            }
        } else {
            if (buffer[0] != 0xFE) {
                return EIO;
            }
        }

        /*
        * Get the next in the chain.
        */
        current_inode = buffer[1];
        current_inode |= (ino_t) buffer[2] << 8;
        current_inode |= (ino_t) buffer[3] << 16;
    }

    status = demofs_read_inode(fs, current_inode, buffer);
    if (status != 0) {
        return status;
    }

    /*
    * Check if we've gone past the end of the directory.
    */
    if (buffer[offset * 32] == 0) {
        return 0;
    }

    /*
    * strncpy is a bit iffy, so let's just play it safe.
    */
    char name[MAX_NAME_LENGTH + 2];
    memset(name, 0, MAX_NAME_LENGTH + 2);
    strncpy(name, (char*) buffer + offset * 32, MAX_NAME_LENGTH);
    strcpy(dir.d_name, name);

    dir.d_namlen = strlen(name);

    ino_t inode = buffer[offset * 32 + MAX_NAME_LENGTH + 4];
    inode |= (ino_t) buffer[offset * 32 + MAX_NAME_LENGTH + 5] << 8;
    inode |= (ino_t) buffer[offset * 32 + MAX_NAME_LENGTH + 6] << 16;

    LogPrintf("entry no = %d. offset = %d. indirs = %d. name = %s\n", entry_number, offset, indirections, name);
    dir.d_ino = inode & 0x7FFFFFFF;
    dir.d_disk = (size_t) fs->disk->node->stat.st_dev;
    dir.d_type = (buffer[offset * 32 + MAX_NAME_LENGTH + 7] & 1) ? DT_DIR : DT_REG;

    /* Perform the transfer to the correct location */
    return PerformTransfer(&dir, io, sizeof(struct dirent));
}

/*
* ---------------------------------------------------------------------------
* The filesystem thread
* ---------------------------------------------------------------------------
*/

static void DemofsDriverThread(void* _fs) {
    BeginNewThread();

    struct demofs* fs = _fs;

    while (true) {
        struct demofs_request req;
        if (KeGetMessage(fs->msgbox, &req, TIMEOUT_INFINITE) != 0) {
            continue;
        }

        int status;
        if (req.is_directory) {
            status = demofs_read_directory_entry_sync(fs, req.inode, &req.aio->transfer);
        } else {
            status = demofs_read_file_sync(fs, req.inode, req.file_size, &req.aio->transfer);
        }

        /* The aio may be freed by the waiter as soon as this is called. */
        SignalDoneIO(req.aio, status);
    }
}

/*
* Creates the message box and thread for a demofs. Call this once, when the
* filesystem is mounted, before any reads can happen.
*/
int demofs_start_driver(struct demofs* fs) {
    fs->msgbox = CreateMessageBox(sizeof(struct demofs_request), DEMOFS_QUEUE_DEPTH);
    if (fs->msgbox == NULL) {
        return ENOMEM;
    }

    CreateThread(GetKernelVas(), DemofsDriverThread, fs);
    return 0;
}

/*
* Reads from a file into the transfer in aio. 'file' is the file's first sector
* and 'file_size' is the total length of the file.
*
* This only queues the request. Returns an error if it could not be queued
* (nothing is signalled in that case). Otherwise returns 0 and the read completes
* when the filesystem thread has finished, which the caller finds out about with
* WaitIO.
*/
int demofs_read_file(struct demofs* fs, ino_t file, uint32_t file_size, struct aio* aio) {
    struct demofs_request req = {
        .is_directory = false,
        .inode = file,
        .file_size = file_size,
        .aio = aio,
    };

    return KePostMessage(fs->msgbox, (const void*) &req, TIMEOUT_INSTANT);
}

/*
* As above, for reading the next directory entry into the transfer.
*/
int demofs_read_directory_entry(struct demofs* fs, ino_t directory, struct aio* aio) {
    struct demofs_request req = {
        .is_directory = true,
        .inode = directory,
        .file_size = 0,
        .aio = aio,
    };

    return KePostMessage(fs->msgbox, (const void*) &req, TIMEOUT_INSTANT);
}