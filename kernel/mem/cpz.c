#include <obj.h>
#include <heap.h>
#include <common.h>
#include <errno.h>
#include <string.h>
#include <vmm.h>
#include <vfs.h>
#include <aio.h>
#include <cpz.h>
#include <arch.h>

/*
 * CPZ: images stored as independently compressed 4096 byte pages.
 *
 *   0   'C' 'P' 'Z' 0           signature, version 0
 *   4   uint16  pages           pages in the ORIGINAL (zero padded) image
 *   6   uint16  tail            bytes of the original in the last page (1..4095), 0 = that page is full (or the file
 *                               predates this field: it then reads as whole pages)
 *   8   uint32  table[pages+1]  bits 31..29 = type, bits 28..0 = absolute file offset of the page's data
 *   ..  page data
 * All little-endian. A page's stored length is (next entry's offset) - (its own). The extra last entry is the end
 * marker. Types:  000 stored as is (4096 bytes)   001 dictionary mode   010 all zeros (stores nothing)
 *                 100 RLE mode                    110 end marker (last entry only)   011, 101, 111 invalid.
 * RLE and dictionary pages are stored only if the stream is shorter than 4090 bytes.
 */

#define CPZ_PAGE_SIZE       4096
#define CPZ_HEADER_SIZE     8
#define CPZ_RAW_LIMIT       4090        /* a page whose compressed form is this big or bigger is stored raw */

#define CPZ_PAGE_RAW        0
#define CPZ_PAGE_DICT       1
#define CPZ_PAGE_ZERO       2
#define CPZ_PAGE_RLE        4
#define CPZ_ENTRY_END       6

/* Dictionary mode ops (see KeCpzDecodeDict). */
#define CPZ_DICT_MAX        128         /* entries 0..127 are the ops 0x80..0xFF */

/*
 * Bytes of compressed stream held on the stack at a time. Decoding never allocates: the page is expanded straight
 * into its own frame, the dictionary is CPZ_DICT_MAX uint16_t on the stack, and the compressed stream is fed
 * through this window (a literal longer than the window is read straight into the frame instead).
 * Any size >= 1 works; a bigger one means fewer file reads per page.
 */
#ifndef CPZ_WINDOW
#define CPZ_WINDOW          256
#endif

struct cpz_page {
    size_t   offset;        /* absolute file offset of the stored data */
    uint16_t length : 13;   /* number of bytes stored there (0 for a zero page, 4096 for a raw one) */
    uint16_t type : 3;
} __attribute__((packed));

_Static_assert(PAGE_SIZE == CPZ_PAGE_SIZE, "CPZ images are made of 4096 byte pages");
_Static_assert(CPZ_PAGE_SIZE < (1 << 13), "a page's stored length must fit cpz_page.length");
_Static_assert(CPZ_PAGE_SIZE <= (1 << 12), "dictionary entries keep their page offset in 12 bits");
_Static_assert(CPZ_ENTRY_END < (1 << 3), "types are 3 bits");

#define ENTRY_TYPE(e)       ((unsigned) ((e) >> 29))
#define ENTRY_OFFSET(e)     ((size_t) ((e) & 0x1FFFFFFFu))

static uint32_t KeLoadLE32(const uint8_t* p) {
    return (uint32_t) p[0] | ((uint32_t) p[1] << 8) | ((uint32_t) p[2] << 16) | ((uint32_t) p[3] << 24);
}

/* On success: 'pages' pages in the table and the original is 'size' bytes long (<= pages * 4096). */
static int KeCpzParseHeader(const uint8_t header[CPZ_HEADER_SIZE], size_t* pages, size_t* size) {
    if (header[0] != 'C' || header[1] != 'P' || header[2] != 'Z' || header[3] != 0) {
        return EINVAL;
    }
    size_t count = (size_t) header[4] | ((size_t) header[5] << 8);
    size_t tail  = (size_t) header[6] | ((size_t) header[7] << 8);
    if (count == 0 || tail >= CPZ_PAGE_SIZE) {
        return EINVAL;
    }
    *pages = count;
    *size = (count - 1) * CPZ_PAGE_SIZE + (tail != 0 ? tail : CPZ_PAGE_SIZE);
    return 0;
}

static size_t KeCpzTableBytes(size_t pages) {
    return (pages + 1) * sizeof(uint32_t);
}

/* The table is little-endian on disk; this converts what was just read, in place. */
static void KeCpzLoadTable(uint32_t* table, size_t pages) {
    for (size_t i = 0; i <= pages; ++i) {
        table[i] = KeLoadLE32((const uint8_t*) &table[i]);
    }
}

static int KeCpzValidateTable(const uint32_t* table, size_t pages) {
    if (pages == 0) {
        return EINVAL;
    }
    if (ENTRY_TYPE(table[pages]) != CPZ_ENTRY_END) {
        return EINVAL;
    }

    size_t data_start = CPZ_HEADER_SIZE + KeCpzTableBytes(pages);

    for (size_t i = 0; i < pages; ++i) {
        unsigned type = ENTRY_TYPE(table[i]);
        size_t offset = ENTRY_OFFSET(table[i]);
        size_t next   = ENTRY_OFFSET(table[i + 1]);

        if (offset < data_start || next < offset) {
            return EINVAL;
        }
        size_t length = next - offset;

        switch (type) {
        case CPZ_PAGE_ZERO:
            if (length != 0) return EINVAL;
            break;
        case CPZ_PAGE_RAW:
            if (length != CPZ_PAGE_SIZE) return EINVAL;
            break;
        case CPZ_PAGE_RLE:
        case CPZ_PAGE_DICT:
            if (length == 0 || length >= CPZ_RAW_LIMIT) return EINVAL;
            break;
        default:
            return EINVAL;      /* 110 is only legal as the end marker; 011, 101 and 111 are unused */
        }
    }
    return 0;
}

static int KeCpzGetPage(const uint32_t* table, size_t pages, size_t index, struct cpz_page* out) {
    if (index >= pages) {
        return EINVAL;
    }
    out->type   = (uint16_t) ENTRY_TYPE(table[index]);
    out->offset = ENTRY_OFFSET(table[index]);
    out->length = (uint16_t) (ENTRY_OFFSET(table[index + 1]) - out->offset);      /* validated: <= 4096 */
    return 0;
}

/*
 * What a CPZ mapping needs to reload a page is the file's table: where each 4096 byte page of the ORIGINAL
 * image sits in the compressed file, and how it is stored. That is read once, validated once, and kept here.
 * It is the loader's context_p for every mapping made from it, and it holds a reference on the file so the
 * file can't vanish under it.
 */
struct cpz_image {
    struct obj_header hdr;
    struct file* file;
    uint32_t pages;             /* pages in the decompressed image */
    size_t size;                /* its exact size in bytes: what the caller's file was before it was compressed */
    uint32_t table[];           /* pages + 1 entries, host byte order, already validated */
};

static void KeCleanupCpzImage(void* _img) {
    struct cpz_image* img = _img;
    KeDerefObject(img->file);
    FreeHeap(img);
}

void KeInitCpzImages(void) {
    KeRegisterObjectType(OBJ_CPZ_INST, KeCleanupCpzImage);
}

/*
 * Reads 'len' bytes at 'offset' into 'buf'. Same pattern as VmmDefaultReload(): a read that runs off the end
 * of the file is not an error, it just leaves the rest of 'buf' as it was, so callers zero it first and treat
 * what comes back as untrusted.
 */
static int KeCpzReadFile(struct file* file, size_t offset, void* buf, size_t len) {
    struct aio aio;
    struct transfer tr = KeCreateKernelTransfer(buf, len, offset, TRANSFER_READ);
    KeCreateIO(&aio, file, tr, NULL, NULL);
    int res = KeReadFile(&aio);
    if (res == 0) {
        res = KeWaitIO(&aio, TIMEOUT_INFINITE);
    }
    return res;
}

/* ========================================================================
 * Decoding. A page is expanded directly into its frame; nothing is allocated.
 * ====================================================================== */

/* The compressed stream of one page, read from the file a window at a time. */
struct cpz_in {
    struct file* file;
    size_t next;                /* file offset of the next stream byte that has not been read from the file yet */
    size_t left;                /* stream bytes not yet read from the file */
    size_t pos, end;            /* win[pos..end) has been read from the file but not consumed */
    uint8_t win[CPZ_WINDOW];
};

static void KeInInit(struct cpz_in* in, struct file* file, size_t offset, size_t length) {
    in->file = file;
    in->next = offset;
    in->left = length;
    in->pos = in->end = 0;
}

static int KeInRefill(struct cpz_in* in) {
    size_t n = in->left < CPZ_WINDOW ? in->left : CPZ_WINDOW;
    if (n == 0) {
        return EIO;             /* the stream ended before the page was complete */
    }
    memset(in->win, 0, n);      /* a short read must not leave stack garbage to be decoded */
    int res = KeCpzReadFile(in->file, in->next, in->win, n);
    if (res != 0) {
        return res;
    }
    in->next += n;
    in->left -= n;
    in->pos = 0;
    in->end = n;
    return 0;
}

static int KeInByte(struct cpz_in* in, uint8_t* byte) {
    if (in->pos == in->end) {
        int res = KeInRefill(in);
        if (res != 0) return res;
    }
    *byte = in->win[in->pos++];
    return 0;
}

/* Copies the next 'n' stream bytes to 'dst', which must already be zeroed (a short read leaves it as it was). */
static int KeInCopy(struct cpz_in* in, uint8_t* dst, size_t n) {
    while (n > 0) {
        if (in->pos == in->end) {
            if (n >= CPZ_WINDOW && in->left >= n) {
                int res = KeCpzReadFile(in->file, in->next, dst, n);      /* a long run: straight into the frame */
                if (res != 0) return res;
                in->next += n;
                in->left -= n;
                return 0;
            }
            int res = KeInRefill(in);
            if (res != 0) return res;
        }
        size_t k = in->end - in->pos;
        if (k > n) k = n;
        memcpy(dst, in->win + in->pos, k);
        in->pos += k;
        dst += k;
        n -= k;
    }
    return 0;
}

/* The stream must have been used up exactly: trailing bytes mean the table or the stream is wrong. */
static int KeInFinished(const struct cpz_in* in) {
    return in->pos == in->end && in->left == 0;
}

/*
 * RLE mode. Each op byte is 2 type bits + 6 value bits:
 *   00vvvvvv  literal: (v + 1) bytes follow
 *   01vvvvvv  (v + 1) zero bytes, nothing follows
 *   10vvvvvv  (v + 1) copies of the single byte that follows
 *   11vvvvvv  prefix: v becomes the high bits of the NEXT non-prefix op's count (6 bits per prefix, stackable)
 * 'out' must be zeroed.
 */
static int KeCpzDecodeRle(struct cpz_in* in, uint8_t out[CPZ_PAGE_SIZE]) {
    size_t op = 0;
    size_t value = 0;

    while (op < CPZ_PAGE_SIZE) {
        uint8_t byte;
        int res = KeInByte(in, &byte);
        if (res != 0) return res;

        /*
         * 'value' is (count - 1) so far. Shifting 6 more bits in is only survivable while it can still
         * be a legal count, i.e. <= 4095. Anything bigger can never fit in a page; stop before it can
         * wrap, however many prefixes the stream stacks.
         */
        if (value > (CPZ_PAGE_SIZE >> 6) - 1) {
            return EIO;
        }
        value = (value << 6) | (byte & 0x3F);

        unsigned kind = byte >> 6;
        if (kind == 3) {
            continue;                   /* prefix: more high bits for the next op */
        }

        size_t count = value + 1;
        value = 0;
        if (count > CPZ_PAGE_SIZE - op) {
            return EIO;                 /* would overrun the page */
        }

        if (kind == 0) {
            res = KeInCopy(in, out + op, count);
        } else if (kind == 1) {
            res = 0;                    /* the frame is already zero */
        } else {
            uint8_t fill;
            res = KeInByte(in, &fill);
            if (res == 0) memset(out + op, fill, count);
        }
        if (res != 0) return res;
        op += count;
    }

    return KeInFinished(in) ? 0 : EIO;
}

/*
 * Dictionary mode. Op byte:
 *   0x00-0x0F  add     (N + 2) literal bytes follow: output them and remember them as the next dictionary entry
 *                      (2..17 bytes; at most 128 entries; entries are never removed). No prefix allowed.
 *   0x10-0x1F  literal (N + 1) literal bytes follow
 *   0x20-0x2F  zeros   (N + 2) zero bytes
 *   0x30-0x3F  prefix  its low 4 bits are higher bits of the NEXT op's N, which must be a 0x10, 0x20 or 0x40 op;
 *                      stackable, first = most significant
 *   0x40-0x4F  repeat  the single byte that follows, (N + 2) times
 *   0x50-0x7F  zeros   (op - 0x50 + 18) zero bytes, no prefix allowed
 *   0x80-0xFF  entry   output dictionary entry (op - 0x80), which must already exist; no prefix allowed
 *
 * The dictionary needs no copy of the data: an entry is just where its bytes already ARE in the page being
 * decoded. dict[k] = (length - 2) << 12 | offset in the page. 'out' must be zeroed.
 */
static int KeCpzDecodeDict(struct cpz_in* in, uint8_t out[CPZ_PAGE_SIZE]) {
    uint16_t dict[CPZ_DICT_MAX];
    unsigned entries = 0;
    size_t op = 0;
    size_t acc = 0;                     /* N so far, from prefix ops */
    int prefixed = 0;

    while (op < CPZ_PAGE_SIZE) {
        uint8_t byte;
        int res = KeInByte(in, &byte);
        if (res != 0) return res;

        if (byte >= 0x80) {             /* an entry */
            unsigned k = byte - 0x80u;
            if (prefixed || k >= entries) return EIO;
            size_t off = dict[k] & 0x0FFF;
            size_t len = (size_t) (dict[k] >> 12) + 2;
            if (len > CPZ_PAGE_SIZE - op) return EIO;
            memcpy(out + op, out + off, len);       /* the entry ended before 'op', so these can't overlap */
            op += len;
            continue;
        }

        unsigned kind = byte >> 4;
        if (kind == 3) {                /* prefix */
            if (acc > CPZ_PAGE_SIZE) return EIO;    /* can only grow: it could never be a legal count again */
            acc = (acc << 4) | (byte & 0x0F);
            prefixed = 1;
            continue;
        }

        if (kind >= 5) {                /* 18..65 zeros in one byte */
            size_t count = (size_t) (byte - 0x50) + 18;
            if (prefixed || count > CPZ_PAGE_SIZE - op) return EIO;
            op += count;                /* the frame is already zero */
            continue;
        }

        if (kind == 0 && (prefixed || entries >= CPZ_DICT_MAX)) {
            return EIO;                 /* an add can't take a prefix, and the dictionary may be full */
        }
        size_t count = ((acc << 4) | (byte & 0x0F)) + (kind == 1 ? 1 : 2);
        acc = 0;
        prefixed = 0;
        if (count > CPZ_PAGE_SIZE - op) return EIO;

        if (kind == 2) {                /* zeros */
            res = 0;
        } else if (kind == 4) {         /* repeat */
            uint8_t fill;
            res = KeInByte(in, &fill);
            if (res == 0) memset(out + op, fill, count);
        } else {                        /* literal, or add */
            res = KeInCopy(in, out + op, count);
            if (res == 0 && kind == 0) {
                dict[entries++] = (uint16_t) (((count - 2) << 12) | op);
            }
        }
        if (res != 0) return res;
        op += count;
    }

    return KeInFinished(in) ? 0 : EIO;
}

/* Expands one RLE or dictionary page of 'file' into 'out' (which must be zeroed). */
static int KeCpzDecodePage(struct file* file, const struct cpz_page* page, uint8_t out[CPZ_PAGE_SIZE]) {
    struct cpz_in in;
    KeInInit(&in, file, page->offset, page->length);
    return page->type == CPZ_PAGE_DICT ? KeCpzDecodeDict(&in, out) : KeCpzDecodeRle(&in, out);
}

/* ========================================================================
 * Images and mappings
 * ====================================================================== */

/*
 * Reads the first 3 bytes of 'file' and sets *is_cpz to whether they are the CPZ signature ('C' 'P' 'Z'). Returns 0, or the
 * read's error: a file that could not be read is NOT "not CPZ", and the caller must not go on to treat it as plain data.
 * A file shorter than 3 bytes is simply not CPZ. This says nothing about whether the rest is sound: KeOpenCpzImage() decides that.
 */
export int KeCheckCpzSignature(struct file* file, bool* is_cpz) {
    uint8_t sig[3] = { 0, 0, 0 };
    *is_cpz = false;
    if (file == NULL) {
        return EINVAL;
    }
    int res = KeCpzReadFile(file, 0, sig, sizeof(sig));
    if (res != 0) {
        return res;
    }
    *is_cpz = sig[0] == 'C' && sig[1] == 'P' && sig[2] == 'Z';
    return 0;
}

export struct cpz_image* KeOpenCpzImage(struct file* file) {
    if (file == NULL) {
        return NULL;
    }

    uint8_t header[CPZ_HEADER_SIZE];
    memset(header, 0, sizeof(header));
    if (KeCpzReadFile(file, 0, header, sizeof(header)) != 0) {
        return NULL;
    }

    size_t pages, size;
    if (KeCpzParseHeader(header, &pages, &size) != 0) {
        return NULL;
    }

    size_t table_bytes = KeCpzTableBytes(pages);
    struct cpz_image* img = AllocHeap(sizeof(*img) + table_bytes);
    if (img == NULL) {
        return NULL;
    }
    KeInitObject(img, OBJ_CPZ_INST);
    KeRefObject(file);
    img->file = file;
    img->pages = (uint32_t) pages;
    img->size = size;
    memset(img->table, 0, table_bytes);

    if (KeCpzReadFile(file, CPZ_HEADER_SIZE, img->table, table_bytes) != 0) {
        KeDerefObject(img);
        return NULL;
    }
    KeCpzLoadTable(img->table, pages);

    /* A truncated file reads back as zeros, which fails this (no end marker). */
    if (KeCpzValidateTable(img->table, pages) != 0) {
        KeDerefObject(img);
        return NULL;
    }
    return img;
}

/* The size of the original file. (Mapping it gives zeros from there to the end of the last page, like any file.) */
export size_t KeGetCpzImageSize(struct cpz_image* image) {
    return image->size;
}

/*
 * The reload_func for CPZ mappings. For this loader po->file_offset is a position in the DECOMPRESSED image
 * (KeAllocCompressedFileMemory() starts the range at the offset it was given and the VMM adds PAGE_SIZE per
 * page), so it picks the table entry; the compressed bytes themselves are read from po->file at the offset
 * the table gives.
 *
 * Runs under po->mtx, may block, and may be running for several pages of one mapping at once: it only reads
 * the (immutable) image, and everything it writes is on its own frame or its own stack. It allocates nothing.
 */
static int KeCpzReload(size_t frame, struct virt_loader* vr, struct page_origin* po) {
    struct cpz_image* img = vr->context_p;
    if (img == NULL || !po->is_file) {
        return EINVAL;
    }

    struct cpz_page page;
    int res = KeCpzGetPage(img->table, img->pages, po->file_offset / PAGE_SIZE, &page);
    if (res != 0) {
        return res;
    }

    uint8_t* out = VmmMapFrame(frame);
    if (out == NULL) {
        return ENOMEM;
    }

    /* Whatever the type, the page starts as zeros: a zero page is done, and the decoders and a short read rely on it. */
    memset(out, 0, PAGE_SIZE);

    switch (page.type) {
    case CPZ_PAGE_ZERO:
        res = 0;
        break;

    case CPZ_PAGE_RAW:
        res = KeCpzReadFile(po->file, page.offset, out, PAGE_SIZE);
        break;

    case CPZ_PAGE_RLE:
    case CPZ_PAGE_DICT:
        res = KeCpzDecodePage(po->file, &page, out);
        break;

    default:
        res = EINVAL;
        break;
    }

    if (res != 0) {
        memset(out, 0, PAGE_SIZE);      /* don't leave half a page of somebody's data in a frame we hand back */
    }
    VmmUnmapFrame(out);
    return res;
}

export void* KeAllocCompressedFileMemory(struct cpz_image* image, size_t offset, size_t bytes, int flags) {
    if (image == NULL || bytes == 0 || offset % PAGE_SIZE != 0) {
        return NULL;
    }

    size_t image_bytes = image->size;
    if (offset >= image_bytes || bytes > image_bytes - offset) {
        return NULL;
    }

    struct virt_loader loader;
    memset(&loader, 0, sizeof(loader));
    loader.reload_func = KeCpzReload;
    loader.context_s = sizeof(*image) + KeCpzTableBytes(image->pages);
    loader.context_p = image;

    return KeAllocFileMemory(image->file, offset, bytes, flags, &loader);
}
