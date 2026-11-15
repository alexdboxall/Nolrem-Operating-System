#include <obj.h>
#include <heap.h>
#include <common.h>
#include <errno.h>
#include <string.h>
#include <vmm.h>
#include <vfs.h>
#include <aio.h>
#include <cpz.h>
#include <unaligned.h>
#include <arch.h>

#define CPZ_PAGE_SIZE       4096
#define CPZ_HEADER_SIZE     8
#define CPZ_RAW_LIMIT       4090        /* a page whose compressed form is this big or bigger is stored raw */
 
#define CPZ_PAGE_RAW        0
#define CPZ_PAGE_ZERO       1
#define CPZ_PAGE_COMPRESSED 2
#define CPZ_ENTRY_END       3
 
struct cpz_page {
    unsigned type;          /* CPZ_PAGE_* */
    size_t   offset;        /* absolute file offset of the stored data */
    size_t   length;        /* number of bytes stored there (0 for a zero page) */
};

_Static_assert(PAGE_SIZE == CPZ_PAGE_SIZE, "CPZ images are made of 4096 byte pages");

#define ENTRY_TYPE(e)       ((unsigned) ((e) >> 30))
#define ENTRY_OFFSET(e)     ((size_t) ((e) & 0x3FFFFFFFu))

static int CpzParseHeader(const uint8_t header[CPZ_HEADER_SIZE], size_t* pages) {
    if (header[0] != 'C' || header[1] != 'P' || header[2] != 'Z' || header[3] != 0) {
        return EINVAL;
    }
    size_t count = (size_t) header[4] | ((size_t) header[5] << 8);
    if (count == 0) {
        return EINVAL;
    }
    *pages = count;
    return 0;
}

static size_t CpzTableBytes(size_t pages) {
    return (pages + 1) * sizeof(uint32_t);
}

static void CpzLoadTable(uint32_t* table, size_t pages) {
    for (size_t i = 0; i <= pages; ++i) {
        table[i] = ReadUnaligned32BE(&table[i]);
    }
}

static int CpzValidateTable(const uint32_t* table, size_t pages) {
    if (pages == 0) {
        return EINVAL;
    }
    if (ENTRY_TYPE(table[pages]) != CPZ_ENTRY_END) {
        return EINVAL;
    }

    size_t data_start = CPZ_HEADER_SIZE + CpzTableBytes(pages);

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
        case CPZ_PAGE_COMPRESSED:
            if (length == 0 || length >= CPZ_RAW_LIMIT) return EINVAL;
            break;
        default:
            return EINVAL;      /* type 3 is only legal as the end marker */
        }
    }
    return 0;
}

static int CpzGetPage(const uint32_t* table, size_t pages, size_t index, struct cpz_page* out) {
    if (index >= pages) {
        return EINVAL;
    }
    out->type   = ENTRY_TYPE(table[index]);
    out->offset = ENTRY_OFFSET(table[index]);
    out->length = ENTRY_OFFSET(table[index + 1]) - out->offset;
    return 0;
}

static int CpzDecompressPage(const uint8_t* in, size_t in_len, uint8_t out[CPZ_PAGE_SIZE]) {
    size_t ip = 0;
    size_t op = 0;
    size_t value = 0;

    while (op < CPZ_PAGE_SIZE) {
        if (ip >= in_len) {
            return EIO;                 /* ran out of input before the page was full */
        }
        uint8_t byte = in[ip++];

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
            if (count > in_len - ip) return EIO;
            memcpy(out + op, in + ip, count);
            ip += count;
        } else if (kind == 1) {
            memset(out + op, 0, count);
        } else {
            if (ip >= in_len) return EIO;
            memset(out + op, in[ip++], count);
        }
        op += count;
    }

    return ip == in_len ? 0 : EIO;      /* trailing bytes mean the table or the stream is wrong */
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
    uint32_t table[];           /* pages + 1 entries, host byte order, already validated */
};

static void CleanupCpzImage(void* _img) {
    struct cpz_image* img = _img;
    DerefObject(img->file);
    FreeHeap(img);
}

void InitCpzImages(void) {
    RegisterObjectType(OBJTYPE_CPZ_INST, CleanupCpzImage);
}

/*
 * Reads 'len' bytes at 'offset' into 'buf'. Same pattern as VmmDefaultReload(): a read that runs off the end
 * of the file is not an error, it just leaves the rest of 'buf' as it was, so callers zero it first and treat
 * what comes back as untrusted.
 */
static int CpzReadFile(struct file* file, size_t offset, void* buf, size_t len) {
    struct aio aio;
    struct transfer tr = CreateKernelTransfer(buf, len, offset, TRANSFER_READ);
    CreateIO(&aio, file, tr, NULL, NULL);
    int res = ReadFile(&aio);
    if (res == 0) {
        res = WaitIO(&aio, TIMEOUT_INFINITE);
    }
    return res;
}

export struct cpz_image* OpenCpzImage(struct file* file) {
    if (file == NULL) {
        return NULL;
    }

    uint8_t header[CPZ_HEADER_SIZE];
    memset(header, 0, sizeof(header));
    if (CpzReadFile(file, 0, header, sizeof(header)) != 0) {
        return NULL;
    }

    size_t pages;
    if (CpzParseHeader(header, &pages) != 0) {
        return NULL;
    }

    size_t table_bytes = CpzTableBytes(pages);
    struct cpz_image* img = AllocHeap(sizeof(*img) + table_bytes);
    if (img == NULL) {
        return NULL;
    }
    InitObject(img, OBJTYPE_CPZ_INST);
    RefObject(file);
    img->file = file;
    img->pages = (uint32_t) pages;
    memset(img->table, 0, table_bytes);

    if (CpzReadFile(file, CPZ_HEADER_SIZE, img->table, table_bytes) != 0) {
        DerefObject(img);
        return NULL;
    }
    CpzLoadTable(img->table, pages);

    /* A truncated file reads back as zeros, which fails this (no end marker). */
    if (CpzValidateTable(img->table, pages) != 0) {
        DerefObject(img);
        return NULL;
    }
    return img;
}

export size_t GetCpzImageSize(struct cpz_image* image) {
    return (size_t) image->pages * PAGE_SIZE;
}

/*
 * The reload_func for CPZ mappings. For this loader po->file_offset is a position in the DECOMPRESSED image
 * (AllocCompressedFileMemory() starts the range at the offset it was given and the VMM adds PAGE_SIZE per
 * page), so it picks the table entry; the compressed bytes themselves are read from po->file at the offset
 * the table gives.
 *
 * Runs under po->mtx, may block, and may be running for several pages of one mapping at once: it only reads
 * the (immutable) image, and everything it writes is on its own frame or its own temporary buffer.
 */
static int CpzReload(size_t frame, struct virt_loader* vr, struct page_origin* po) {
    struct cpz_image* img = vr->context_p;
    if (img == NULL || !po->is_file) {
        return EINVAL;
    }

    struct cpz_page page;
    int res = CpzGetPage(img->table, img->pages, po->file_offset / PAGE_SIZE, &page);
    if (res != 0) {
        return res;
    }

    uint8_t* out = VmmMapFrame(frame);
    if (out == NULL) {
        return ENOMEM;
    }

    switch (page.type) {
    case CPZ_PAGE_ZERO:
        memset(out, 0, PAGE_SIZE);
        res = 0;
        break;

    case CPZ_PAGE_RAW:
        memset(out, 0, PAGE_SIZE);
        res = CpzReadFile(po->file, page.offset, out, PAGE_SIZE);
        break;

    case CPZ_PAGE_COMPRESSED: {
        /*
         * Can't decompress in place: a long run near the start of the stream would overwrite compressed
         * bytes that haven't been read yet. The table was validated, so this is at most 4089 bytes.
         */
        uint8_t* packed = AllocHeap(page.length);
        if (packed == NULL) {
            res = ENOMEM;
            break;
        }
        memset(packed, 0, page.length);
        res = CpzReadFile(po->file, page.offset, packed, page.length);
        if (res == 0) {
            res = CpzDecompressPage(packed, page.length, out);
        }
        FreeHeap(packed);
        break;
    }

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

export void* AllocCompressedFileMemory(struct cpz_image* image, size_t offset, size_t bytes, int flags) {
    if (image == NULL || bytes == 0 || offset % PAGE_SIZE != 0) {
        return NULL;
    }

    size_t image_bytes = GetCpzImageSize(image);
    if (offset >= image_bytes || bytes > image_bytes - offset) {
        return NULL;
    }

    struct virt_loader loader;
    memset(&loader, 0, sizeof(loader));
    loader.reload_func = CpzReload;
    loader.context_s = sizeof(*image) + CpzTableBytes(image->pages);
    loader.context_p = image;

    return AllocFileMemory(image->file, offset, bytes, flags, &loader);
}