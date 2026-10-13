
#include <common.h>
#include <sem.h>
#include <log.h>
#include <interrupt.h>
#include <thread.h>
#include <vfs.h>
#include <string.h>
#include <phys.h>
#include <transfer.h>
#include <assert.h>
#include <errno.h>
#include <heap.h>
#include <arch.h>
#include <stdatomic.h>
#include <vmm.h>
#include <msgbox.h>
#include <stdlib.h>
#include <scheduler.h>
#include <sys/stat.h>
#include <dirent.h>
#include <timer.h>
#include <diskutil.h>
#include <machine/x86.h>


/*
 * Geometry of a 1.44MB floppy.
 */
#define SECTOR_SIZE         512
#define SECTORS_PER_TRACK   18
#define HEADS               2
#define CYLINDERS           80
#define TOTAL_SECTORS       (SECTORS_PER_TRACK * HEADS * CYLINDERS)
#define FLOPPY_SIZE         ((uint64_t) TOTAL_SECTORS * SECTOR_SIZE)

#define FLOPPY_MAX_RUN      (PAGE_SIZE / SECTOR_SIZE)

_Static_assert(FLOPPY_MAX_RUN >= 1 && FLOPPY_MAX_RUN <= SECTORS_PER_TRACK,
               "FLOPPY_MAX_RUN must be between 1 and one track");

#define DMA_BUFFER_SIZE     (FLOPPY_MAX_RUN * SECTOR_SIZE)

/*
 * Registers, as offsets from the controller's base I/O port.
 */
#define FLOPPY_DOR      2
#define FLOPPY_MSR      4
#define FLOPPY_FIFO     5
#define FLOPPY_CCR      7

#define DOR_RESET       0x00    /* Controller held in reset. */
#define DOR_MOTOR_OFF   0x0C    /* Normal operation, IRQ+DMA on, drive 0, motor off. */
#define DOR_MOTOR_ON    0x1C    /* As above, but with the drive 0 motor on. */

#define MSR_RQM         0x80    /* OK (or mandatory) to exchange a byte with the FIFO. */
#define MSR_DIO         0x40    /* Set: controller -> CPU. Clear: CPU -> controller. */

/*
 * Commands.
 */
#define CMD_SPECIFY     0x03
#define CMD_WRITE       0x05
#define CMD_READ        0x06
#define CMD_RECALIBRATE 0x07
#define CMD_SENSE_INT   0x08
#define CMD_SEEK        0x0F
#define CMD_VERSION     0x10
#define CMD_CONFIGURE   0x13

#define CMD_FLAG_MFM    0x40    /* MFM encoding. Always set for reads. */

#define FDC_VERSION_ENHANCED    0x90    /* 82077AA, the only one with CONFIGURE. */

/*
 * CONFIGURE parameter byte layout:
 *   bit 6    EIS:   1 = implied seek on
 *   bit 5    EFIFO: 1 = FIFO DISABLED
 *   bit 4    POLL:  1 = drive polling DISABLED
 *   bits 0-3 FIFOTHR: FIFO threshold, minus one
 *
 * We want explicit seeks, the FIFO on, and drive polling ON, so all three flag
 * bits are left clear.
 */
#define CONFIG_FIFO_THRESHOLD   8

/*
 * Status register bits.
 */
#define ST0_IC          0xC0    /* Interrupt code. 0 = normal, 0xC0 = polling interrupt after a reset. */
#define ST0_SE          0x20    /* Seek end. */
#define ST0_EC          0x10    /* Equipment check (recalibrate never found track 0). */
#define ST0_NR          0x08    /* Drive not ready. */

#define ST1_NOT_WRITABLE    0x02                        /* NW: disk is write protected. */
#define ST1_ERRORS      (0x20 | 0x10 | 0x04 | 0x01)     /* DE, OR, ND, MA */
#define ST2_ERRORS      (0x40 | 0x20 | 0x10 | 0x02 | 0x01)  /* CM, DD, WC, BC, MD */

/*
 * Timings. Generous because real hardware is slow.
 */
#define MOTOR_SPINUP_MS     500
#define MOTOR_IDLE_MS       2000
#define HEAD_SETTLE_MS      15
#define RESET_HOLD_MS       10
#define IRQ_TIMEOUT_RESET_MS    1000
#define IRQ_TIMEOUT_SEEK_MS     3000    /* Recalibrate can take several steps. */
#define IRQ_TIMEOUT_READ_MS     3000
#define MSR_SPIN_COUNT      2000        /* Port reads before we start sleeping. */
#define MSR_TIMEOUT_MS      500
#define MAX_ATTEMPTS        10


#define FLOPPY_TRACE

#ifdef FLOPPY_TRACE
#define FloppyTrace(s) LogPrintf("%s", s)
#else
#define FloppyTrace(s) ((void) 0)
#endif

/*
 * Protects the controller, the motor state, and all fields of floppy_data.
 * Held for the whole of any request, so only one thread uses the floppy at once.
 */
struct mutex* floppy_lock = NULL;

/*
 * IRQ6 handshake. See FloppyIrqArm and FloppyIrqWait.
 */
static struct mutex* floppy_irq_mutex = NULL;
static volatile bool floppy_irq_armed = false;

struct floppy_data {
    int disk_num;
    size_t base;
    struct disk_partition_helper partitions;
    size_t phys_buffer;             /* DMA buffer, below 16MB, never crosses 64KB. */
    size_t virt_buffer;
    int current_cylinder;           /* -1 if unknown. */
    bool need_reset;                /* Protocol desync or timeout: reset before the next command. */
    bool enhanced;                  /* Supports CONFIGURE. */
    volatile bool motor_on;
    struct msgbox* msgbox;
};

/*
 * The IRQ handler releases the IRQ mutex, but only if a command armed it, and
 * only once per command. Anything else (a late IRQ from a command we gave up
 * on, a spurious one) is ignored rather than releasing a mutex that is not
 * held.
 */
static void FloppyIrqHandler(struct x86_regs*) {
    if (__atomic_exchange_n(&floppy_irq_armed, false, __ATOMIC_ACQ_REL)) {
        ReleaseMutex(floppy_irq_mutex);
    }
}

/*
 * The IRQ mutex is free when no command is in flight. Before issuing a command
 * that will raise IRQ6, FloppyIrqArm takes it. The IRQ handler then releases
 * it, and FloppyIrqWait blocks acquiring it (with a timeout) until that
 * happens. Arming BEFORE the command is sent matters: a fast controller can
 * raise the IRQ before we get to the wait, and the wait then simply succeeds.
 *
 * If a previous command was armed but abandoned (e.g. the command bytes timed
 * out, so no wait ever happened) the mutex is still held, and the handler has
 * not released it. Arming cleans that up first, so callers don't need any
 * error-path handling.
 *
 * Callers must hold floppy_lock.
 */
static void FloppyIrqArm(void) {
    if (__atomic_exchange_n(&floppy_irq_armed, false, __ATOMIC_ACQ_REL)) {
        ReleaseMutex(floppy_irq_mutex);
    }

    /*
     * Never blocks: the mutex is free whenever we get here, since only one
     * thread (the one holding floppy_lock) ever uses it.
     */
    AcquireMutex(floppy_irq_mutex, TIMEOUT_INFINITE);
    __atomic_store_n(&floppy_irq_armed, true, __ATOMIC_RELEASE);
}

static int FloppyIrqWait(struct floppy_data* flp, int timeout_ms) {
    if (AcquireMutex(floppy_irq_mutex, MS_TO_NS(timeout_ms)) != 0) {
        /*
         * Timed out. Disarm so a late IRQ can't release the mutex behind our
         * back. If it was still armed, we hold the mutex and must put it back.
         */
        if (__atomic_exchange_n(&floppy_irq_armed, false, __ATOMIC_ACQ_REL)) {
            ReleaseMutex(floppy_irq_mutex);
            LogPrintf("[floppy]: IRQ timeout\n");
            flp->need_reset = true;
            return ETIMEDOUT;
        }

        /*
         * Otherwise the IRQ arrived right at the deadline, and the handler has
         * already released the mutex, so it is free again. Treat it as a success.
         */
        return 0;
    }

    /*
     * Got the IRQ. Hand the mutex back so it is free for the next command.
     */
    ReleaseMutex(floppy_irq_mutex);
    return 0;
}

/*
 * Reading the MSR a few times gives the controller time to update RQM/DIO
 * after a FIFO access. Newer hardware still needs this.
 */
static void FloppySettle(struct floppy_data* flp) {
    for (int i = 0; i < 4; ++i) {
        (void) inb(flp->base + FLOPPY_MSR);
    }
}



static void FloppyMotor(struct floppy_data* flp, bool on) {
    if (on) {
        outb(flp->base + FLOPPY_DOR, DOR_MOTOR_ON);
        if (!flp->motor_on) {
            SleepMilli(MOTOR_SPINUP_MS);
        }
    } else {
        outb(flp->base + FLOPPY_DOR, DOR_MOTOR_OFF);
    }
    flp->motor_on = on;
}

/*
 * Waits until the controller is ready for us to write a byte (RQM=1, DIO=0), or
 * to read a byte (RQM=1, DIO=1). Anything else for too long means we are out
 * of sync with the controller, which can only be fixed with a reset.
 */
static int FloppyWaitMsr(struct floppy_data* flp, bool want_read) {
    const uint8_t mask = MSR_RQM | MSR_DIO;
    const uint8_t want = want_read ? (MSR_RQM | MSR_DIO) : MSR_RQM;

    /* Normally ready within microseconds, so spin first. */
    for (int i = 0; i < MSR_SPIN_COUNT; ++i) {
        if ((inb(flp->base + FLOPPY_MSR) & mask) == want) {
            return 0;
        }
    }

    for (int i = 0; i < MSR_TIMEOUT_MS; ++i) {
        SleepMilli(1);
        if ((inb(flp->base + FLOPPY_MSR) & mask) == want) {
            return 0;
        }
    }

    LogPrintf("[floppy]: MSR timeout\n");
    flp->need_reset = true;
    return ETIMEDOUT;
}

static int FloppyCommand(struct floppy_data* flp, const uint8_t* bytes, int count) {
    for (int i = 0; i < count; ++i) {
        int error = FloppyWaitMsr(flp, false);
        if (error != 0) return error;
        outb(flp->base + FLOPPY_FIFO, bytes[i]);
        FloppySettle(flp);
    }
    return 0;
}

static int FloppyResult(struct floppy_data* flp, uint8_t* bytes, int count) {
    for (int i = 0; i < count; ++i) {
        int error = FloppyWaitMsr(flp, true);
        if (error != 0) return error;
        bytes[i] = inb(flp->base + FLOPPY_FIFO);
        FloppySettle(flp);
    }
    return 0;
}

/*
 * Only call this after an IRQ that needs it (reset in polling mode, seek or
 * recalibrate). Otherwise the controller replies 0x80 and locks up.
 */
static int FloppySenseInterrupt(struct floppy_data* flp, int* st0, int* cyl) {
    static const uint8_t cmd[] = { CMD_SENSE_INT };
    uint8_t res[2];

    int error = FloppyCommand(flp, cmd, sizeof(cmd));
    if (error == 0) error = FloppyResult(flp, res, 2);
    if (error != 0) return error;

    *st0 = res[0];
    *cyl = res[1];
    return 0;
}

/*
 * SEEK and RECALIBRATE both: send command, wait for IRQ, then SENSE INTERRUPT
 * to see if we ended up at the right cylinder.
 */
static int FloppyStep(struct floppy_data* flp, const uint8_t* cmd, int length, int target_cylinder) {
    for (int attempt = 0; attempt < 5; ++attempt) {
        int st0 = -1;
        int cyl = -1;

        FloppyIrqArm();

        int error = FloppyCommand(flp, cmd, length);
        if (error == 0) error = FloppyIrqWait(flp, IRQ_TIMEOUT_SEEK_MS);
        if (error == 0) error = FloppySenseInterrupt(flp, &st0, &cyl);
        if (error != 0) return EIO;

        /*
         * Success is seek-end set, no error bits, and the right cylinder. A
         * recalibrate only steps 79 times, so on a drive that was further out
         * it can legitimately need another go.
         */
        if ((st0 & (ST0_IC | ST0_EC)) == 0 && (st0 & ST0_SE) && cyl == target_cylinder) {
            SleepMilli(HEAD_SETTLE_MS);
            return 0;
        }
    }

    return EIO;
}

/*
 * Move to cylinder 0.
 */
static int FloppyCalibrate(struct floppy_data* flp) {
    static const uint8_t cmd[] = { CMD_RECALIBRATE, 0 };

    FloppyTrace("FloppyCalibrate\n");
    int error = FloppyStep(flp, cmd, sizeof(cmd), 0);
    flp->current_cylinder = (error == 0) ? 0 : -1;
    return error;
}

/*
 * Both heads are bolted together, so only one seek is needed to reach a
 * cylinder, regardless of which head will be used.
 */
static int FloppySeek(struct floppy_data* flp, int cylinder, int head) {
    const uint8_t cmd[] = { CMD_SEEK, head << 2, cylinder };

    int error = FloppyStep(flp, cmd, sizeof(cmd), cylinder);
    flp->current_cylinder = (error == 0) ? cylinder : -1;
    return error;
}

static int FloppySpecify(struct floppy_data* flp) {
    /*
     * SRT = 0xD (6ms step), HUT = 0xF, HLT = 1, DMA mode (NDMA = 0).
     */
    static const uint8_t cmd[] = { CMD_SPECIFY, 0xDF, 0x02 };
    return FloppyCommand(flp, cmd, sizeof(cmd));
}

static int FloppyDetectVersion(struct floppy_data* flp) {
    static const uint8_t cmd[] = { CMD_VERSION };
    uint8_t version = 0;

    int error = FloppyCommand(flp, cmd, sizeof(cmd));
    if (error == 0) error = FloppyResult(flp, &version, 1);
    if (error != 0) return error;

    flp->enhanced = (version == FDC_VERSION_ENHANCED);
    return 0;
}

static int FloppyConfigure(struct floppy_data* flp) {
    /*
     * Implied seek off, FIFO on, drive polling mode ON, threshold 8,
     * precompensation 0 (use the drive default). CONFIGURE settings are lost
     * on reset (we never LOCK them), so this runs after every reset. The
     * polling mode and implied seek settings survive a reset, but the FIFO
     * settings do not.
     */
    const uint8_t cmd[] = { CMD_CONFIGURE, 0x00, CONFIG_FIFO_THRESHOLD - 1, 0x00 };
    return FloppyCommand(flp, cmd, sizeof(cmd));
}

/*
 * Resets the controller, reprograms it, and recalibrates the drive. Leaves the
 * motor on.
 */
static int FloppyReset(struct floppy_data* flp) {
    FloppyTrace("FloppyReset\n");

    flp->need_reset = false;
    flp->current_cylinder = -1;

    /*
     * Hold the controller in reset for a while (it needs at least a few
     * microseconds).
     */
    outb(flp->base + FLOPPY_DOR, DOR_RESET);
    SleepMilli(RESET_HOLD_MS);

    FloppyIrqArm();
    outb(flp->base + FLOPPY_DOR, DOR_MOTOR_OFF);

    /*
     * Writing the DOR turned the motor off, whatever our idea of it was.
     */
    flp->motor_on = false;

    int error = FloppyIrqWait(flp, IRQ_TIMEOUT_RESET_MS);
    if (error != 0) return error;

    /*
     * Drive polling mode: four SENSE INTERRUPTs are required after a reset,
     * one for each of the four possible drives. Each reports 0xC0 | drive.
     */
    for (int i = 0; i < 4; ++i) {
        int st0, cyl;
        error = FloppySenseInterrupt(flp, &st0, &cyl);
        if (error != 0) return error;
    }

    /*
     * 500kbps, for 1.44MB.
     */
    outb(flp->base + FLOPPY_CCR, 0x00);

    error = FloppySpecify(flp);
    if (error != 0) return error;

    /*
     * CONFIGURE only exists on the enhanced controller (82077AA and
     * compatibles). The old ones would treat it as an invalid command and get
     * confused, so check first. Without it we keep the power-on defaults,
     * which also have drive polling mode on.
     */
    error = FloppyDetectVersion(flp);
    if (error != 0) return error;

    if (flp->enhanced) {
        error = FloppyConfigure(flp);
        if (error != 0) return error;
    }

    /* Force it to properly turn on. */
    flp->motor_on = false;
    FloppyMotor(flp, true);

    return FloppyCalibrate(flp);
}

/*
 * Sets up ISA DMA channel 2 to move 'length' bytes between the DMA buffer and
 * the controller: into the buffer for a read, out of it for a write.
 */
static void FloppyDmaInit(struct floppy_data* flp, size_t length, bool write) {
    uint32_t addr = flp->phys_buffer;
    uint16_t count = length - 1;

    outb(0x0A, 0x06);                   /* Mask channel 2. */
    outb(0x0C, 0xFF);                   /* Reset the flip-flop. */
    outb(0x04, (addr >> 0) & 0xFF);     /* Address low, then high. */
    outb(0x04, (addr >> 8) & 0xFF);
    outb(0x81, (addr >> 16) & 0xFF);    /* Page register. */
    outb(0x0C, 0xFF);                   /* Reset the flip-flop. */
    outb(0x05, (count >> 0) & 0xFF);    /* Count - 1, low then high. */
    outb(0x05, (count >> 8) & 0xFF);
    outb(0x0B, write ? 0x4A : 0x46);    /* Single mode, channel 2. 0x4A: memory -> device (write), 0x46: device -> memory (read). */
    outb(0x0A, 0x02);                   /* Unmask channel 2. */
}

/*
 * Reads or writes 'count' consecutive sectors on one track with a single
 * command. The drive must already be on the right cylinder. The data goes to
 * (read) or comes from (write) the start of the DMA buffer, which for a write
 * must already be filled in.
 *
 * Returns EROFS if the disk is write protected, EIO for any other failure.
 */
static int FloppyTransferRun(struct floppy_data* flp, bool write, int cylinder, int head, int sector, int count) {
    const uint8_t cmd[] = {
        (write ? CMD_WRITE : CMD_READ) | CMD_FLAG_MFM,  /* No multi-track: we never cross a track. */
        head << 2,                  /* Head and drive 0. */
        cylinder,
        head,
        sector,                     /* First sector (1-based). */
        2,                          /* 512 bytes per sector. */
        sector + count - 1,         /* Last sector to read. */
        0x1B,                       /* Gap length for 1.44MB. */
        0xFF,                       /* Data length, unused as the size is not 0. */
    };
    uint8_t res[7];

    FloppyDmaInit(flp, count * SECTOR_SIZE, write);

    FloppyIrqArm();

    int error = FloppyCommand(flp, cmd, sizeof(cmd));
    if (error == 0) error = FloppyIrqWait(flp, IRQ_TIMEOUT_READ_MS);
    if (error == 0) error = FloppyResult(flp, res, sizeof(res));
    if (error != 0) return EIO;

    /*
     * st0, st1, st2, cylinder, head, sector, bytes per sector. The DMA
     * controller signals terminal count at the end of the last byte, which is
     * what stops the controller, so a clean transfer has no error bits set at
     * all.
     */
    if (write && (res[1] & ST1_NOT_WRITABLE)) return EROFS;
    if (res[0] & (ST0_IC | ST0_NR)) return EIO;
    if (res[1] & ST1_ERRORS) return EIO;
    if (res[2] & ST2_ERRORS) return EIO;
    if (res[6] != 2) return EIO;

    return 0;
}

/*
 * Reads or writes up to 'count' sectors (at most FLOPPY_MAX_RUN, all on one
 * track), retrying and recovering as needed. Returns how many were actually
 * transferred in 'done'.
 *
 * Read retries fall back to a single sector, the most forgiving option. Write
 * retries always repeat the whole run, since all of its data has already been
 * taken from the caller and staged in the DMA buffer. Rewriting the same sectors
 * is harmless. A write protected disk fails straight away with no retries.
 */
static int FloppyTransferSectors(struct floppy_data* flp, bool write, int cylinder, int head,
                                 int sector, int count, int* done) {
    for (int attempt = 0; attempt < MAX_ATTEMPTS; ++attempt) {
        /*
         * Escalate: plain retry, then recalibrate, then full reset. A
         * protocol timeout always needs the reset.
         */
        if (flp->need_reset || (attempt > 0 && attempt % 6 == 0)) {
            FloppyReset(flp);

        } else if (attempt > 0 && attempt % 3 == 0) {
            FloppyCalibrate(flp);
        }

        if (flp->current_cylinder != cylinder) {
            if (FloppySeek(flp, cylinder, head) != 0) {
                continue;
            }
        }

        int run = (write || attempt < 3) ? count : 1;

        int error = FloppyTransferRun(flp, write, cylinder, head, sector, run);
        if (error == 0) {
            *done = run;
            return 0;
        }
        if (error == EROFS) {
            LogPrintf("[floppy]: disk is write protected\n");
            return EROFS;
        }
    }

    LogPrintf(write ? "[floppy]: write failed\n" : "[floppy]: read failed\n");
    return EIO;
}

static int FloppyIo(struct floppy_data* flp, struct transfer* io) {
    bool write = (io->direction == TRANSFER_WRITE);

    uint64_t offset = io->offset;
    uint64_t length = io->length_remaining;

    if (offset % SECTOR_SIZE != 0 || length % SECTOR_SIZE != 0) {
        LogPrintf("[floppy]: bad offset / length\n");
        return EINVAL;
    }
    if (offset >= FLOPPY_SIZE || length > FLOPPY_SIZE - offset) {
        LogPrintf("[floppy]: out of range\n");
        return EINVAL;
    }
    if (length == 0) {
        return 0;
    }

    int lba = offset / SECTOR_SIZE;
    int remaining = length / SECTOR_SIZE;
    int error = 0;

    AcquireMutex(floppy_lock, TIMEOUT_INFINITE);

    while (remaining > 0) {
        /*
         * Floppies use CHS (cylinder, head, sector) for addressing sectors
         * instead of LBA (linear block addressing). Hence we need to convert
         * to CHS. Note that sector is 1-based, whereas cylinder and head are
         * 0-based.
         */
        int cylinder = lba / (SECTORS_PER_TRACK * HEADS);
        int head = (lba / SECTORS_PER_TRACK) % HEADS;
        int sector = (lba % SECTORS_PER_TRACK) + 1;

        /*
         * Read as many sectors as we can with one command, without going
         * past the end of this track, the end of the request, or the end of
         * the DMA buffer.
         */
        int count = SECTORS_PER_TRACK - (sector - 1);
        if (count > remaining) count = remaining;
        if (count > FLOPPY_MAX_RUN) count = FLOPPY_MAX_RUN;

        /*
         * For a write, take the caller's data into the DMA buffer first. For a
         * read, hand the data over once it has been read successfully.
         */
        if (write) {
            PerformTransfer((void*) flp->virt_buffer, io, count * SECTOR_SIZE);
        }

        int done = 0;
        error = FloppyTransferSectors(flp, write, cylinder, head, sector, count, &done);
        if (error != 0) break;

        if (!write) {
            PerformTransfer((void*) flp->virt_buffer, io, done * SECTOR_SIZE);
        }

        lba += done;
        remaining -= done;
    }

    ReleaseMutex(floppy_lock);

    FloppyTrace("[floppy]: done\n");
    return error;
}

static int ReadWrite(struct vnode* node, struct transfer* io) {
    return FloppyIo(node->data, io);
}

static int Create(struct vnode* node, struct vnode** partition, const char* name, int, mode_t) {
    AcquireMutex(floppy_lock, TIMEOUT_INFINITE);
    struct floppy_data* flp = node->data;
    int res = DiskCreateHelper(&flp->partitions, partition, name);
    ReleaseMutex(floppy_lock);
    return res;
}

static int Follow(struct vnode* node, struct vnode** output, const char* name) {
    AcquireMutex(floppy_lock, TIMEOUT_INFINITE);
    struct floppy_data* flp = node->data;
    int res = DiskFollowHelper(&flp->partitions, output, name);
    ReleaseMutex(floppy_lock);
    return res;
}

static const struct vnode_operations dev_ops = {
    .read           = ReadWrite,
    .write          = ReadWrite,
    .create         = Create,
    .follow         = Follow,
};

static bool DetectFloppy(void) {
    /*
     * Lowest bit of offset 0x10 into the BIOS Data Area tells us if the BIOS
     * detected any floppy drives.
     */
    uint16_t bda_word = *((uint16_t*) (ARCH_KRNL_MAPPING_BASE + 0x410));
    if (!(bda_word & 1)) {
        return false;
    }

    /*
     * The high nibble refers to the first drive. If it is zero, there is no
     * drive. Otherwise, it refers to the drive type.
     */
    uint8_t cmos_val = ReadCmos(0x10);
    return (cmos_val >> 4);
}



void FloppyDriverThread(void* _flp) {
    BeginNewThread();

    struct floppy_data* flp = _flp;
    flp->msgbox = CreateMessageBox(sizeof(void* /* TODO: (struct aio*) */), 24);

    int64_t next_timeout = TIMEOUT_INFINITE;

    while (true) {
        void* msg;
        int retv = KeGetMessage(flp->msgbox, &msg, next_timeout);
        bool have_request = retv == 0;
        FloppyMotor(flp, have_request);

        if (have_request) {
            /* TODO: process the message */
            next_timeout = MS_TO_NS(MOTOR_IDLE_MS);

        } else {
            next_timeout = TIMEOUT_INFINITE;
        }
    }
}

void InitFloppy(void) {
    if (!DetectFloppy()) {
        return;
    }

    size_t phys_buffer = AllocPhys(true);
    size_t virt_buffer = (size_t) AllocFixedMemory(phys_buffer, PAGE_SIZE, VP_WRITE);
    
    /*
     * We will only support the first floppy drive that a system has.
     */
    struct floppy_data* flp = AllocHeap(sizeof(struct floppy_data));

    *flp = (struct floppy_data) {
        .disk_num = 0,
        .base = 0x3F0,
        .phys_buffer = phys_buffer,
        .virt_buffer = virt_buffer,
        .current_cylinder = -1,
        .need_reset = false,
        .enhanced = false,
        .motor_on = false,
    };

    /*
     * The IRQ mutex must exist before the handler is registered, as the
     * handler uses it. It is only created once, and is never freed even if
     * the controller turns out not to work.
     */
    if (floppy_irq_mutex == NULL) {
        floppy_irq_mutex = CreateMutex();
    }
    RegisterInterruptHandler(6, FloppyIrqHandler);

    /*
     * Nothing else can touch the controller yet (no lock, no motor thread, no
     * vnode), so this needs no locking.
     */
    int res = FloppyReset(flp);
    if (res != 0) {
        LogPrintf("FDC doesn't work...\n");
        FreeDiscardedPhys(GetPhysPage(phys_buffer));
        outb(flp->base + FLOPPY_DOR, DOR_MOTOR_OFF);
        FreeHeap(flp);
        return;
    }

    floppy_lock = CreateMutex();
    CreateThread(GetKernelVas(), FloppyDriverThread, flp);

    struct vnode* node = CreateVnode(dev_ops, (struct stat) {
        .st_mode = S_IFBLK | S_IRWXU | S_IRWXG | S_IRWXO,
        .st_nlink = 1,
        .st_blksize = SECTOR_SIZE,
        .st_blocks = TOTAL_SECTORS,
        .st_size = FLOPPY_SIZE,
        .st_dev = NextDevId()
    });
    node->data = flp;

    InitDiskPartitionHelper(&flp->partitions);
    Mount(node, GenerateNewRawDiskName(DISKUTIL_TYPE_FLOPPY));
    CreateDiskPartitions(CreateFile(node, 0, 0, true, true));
    LogPrintf("Floppy setup done.\n");
}
