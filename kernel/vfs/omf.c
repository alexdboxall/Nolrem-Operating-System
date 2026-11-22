#include <common.h>
#include <vmm.h>
#include <vfs.h>
#include <fcntl.h>
#include <omf.h>
#include <cpz.h>
#include <errno.h>
#include <unaligned.h>

/*
 * Opens 'path' read-only and maps all of it at out->ptr, out->len bytes.
 *
 * flags:
 *   OMF_TRANSPARENT_DECOMPRESS   if the file starts with the CPZ signature it is opened as a CPZ image and what is
 *                                mapped is the DECOMPRESSED data: out->ptr / out->len / every OmfRead*() see the
 *                                original file's contents and size, as if it had never been compressed. Pages are
 *                                expanded on demand as they are touched. out->f is still the file on disk.
 *                                A file that has the signature but is not a sound CPZ image is an error (EINVAL),
 *                                not a plain file that happens to start with those letters.
 *   0                            the file is mapped as it is, CPZ or not.
 */
export int KeOpenAndMapFile(const char* path, int flags, struct open_mapped_file* out) {
    if (out == NULL) {
        return EINVAL;
    }
    out->f = NULL;
    out->cpz = NULL;
    out->error = true;
    out->ptr = NULL;
    out->cursor = 0;
    out->len = 0;
    if (path == NULL || (flags & ~OMF_TRANSPARENT_DECOMPRESS) != 0) {
        return EINVAL;
    }
    int res = KeOpenFile(path, O_RDONLY, 0, &out->f);
    if (res != 0) {
        out->f = NULL;
        return res;
    }
    off_t full_size = out->f->node->stat.st_size;
    size_t len = (size_t) full_size;
    if (full_size <= 0 || (uintmax_t) len != (uintmax_t) full_size) {
        OmfClose(out);
        return EINVAL;
    }

    /*
     * A CPZ image takes its own reference on the file, so there is no need to close and reopen it: the file we
     * already have open IS the one the image reads its pages from, and nothing can swap it for another in between.
     */
    bool is_cpz = false;
    if (flags & OMF_TRANSPARENT_DECOMPRESS) {
        res = KeCheckCpzSignature(out->f, &is_cpz);
        if (res != 0) {                 /* couldn't even look: don't guess "plain file" and hand back compressed bytes */
            OmfClose(out);
            return res;
        }
    }
    if (is_cpz) {
        out->cpz = KeOpenCpzImage(out->f);
        if (out->cpz == NULL) {
            OmfClose(out);
            return EINVAL;
        }
        len = KeGetCpzImageSize(out->cpz);
        /* TODO: lock the file as below; for a CPZ image it matters more, the page table was read once at open. */
        out->ptr = KeAllocCompressedFileMemory(out->cpz, 0, len, 0);
    } else {
        /* TODO: we also want to lock the file so that no-one can modify it under
         * us if a part of it gets paged out. */
        out->ptr = KeAllocFileMemory(out->f, 0, len, 0, 0);
    }
    if (out->ptr == NULL) {
        OmfClose(out);
        return ENOMEM;
    }
    out->len = len;
    out->error = false;
    return 0;
}

/* DOUBLE CLOSE IS ALLOWED! */
export void OmfClose(struct open_mapped_file* omf) {
    if (omf == NULL) {
        return;
    }
    if (omf->ptr != NULL) {
        KeFreeMemory(omf->ptr, omf->len);
        omf->ptr = NULL;
    }
    if (omf->cpz != NULL) {
        KeDerefObject(omf->cpz);
        omf->cpz = NULL;
    }
    if (omf->f != NULL) {
        KeDerefObject(omf->f);
        omf->f = NULL;
    }
    omf->error = true;
    omf->len = 0;
    omf->cursor = 0;
}

export void OmfAdvance(struct open_mapped_file* omf, size_t amount) {
    if (omf->cursor + amount < omf->cursor) {
        omf->error = true;
        return;
    }
    omf->cursor += amount;
    if (omf->cursor > omf->len) {
        omf->error = true;
    }
}

export void OmfJump(struct open_mapped_file* omf, size_t offset) {
    omf->cursor = offset;
    if (omf->cursor > omf->len) {
        omf->error = true;
    }
}

export uint8_t OmfRead8(struct open_mapped_file* omf) {
    if (omf->error || omf->cursor >= omf->len) {
        omf->error = true;
        return 0;
    }
    return ReadUnaligned8LE(AddVoidPtr(omf->ptr, omf->cursor));
}

export uint16_t OmfRead16LE(struct open_mapped_file* omf) {
    if (omf->error || omf->len < 2 || (omf->cursor > omf->len - 2)) {
        omf->error = true;
        return 0;
    }
    return ReadUnaligned16LE(AddVoidPtr(omf->ptr, omf->cursor));
}

export uint32_t OmfRead32LE(struct open_mapped_file* omf) {
    if (omf->error || omf->len < 4 || (omf->cursor > omf->len - 4)) {
        omf->error = true;
        return 0;
    }
    return ReadUnaligned32LE(AddVoidPtr(omf->ptr, omf->cursor));
}

export uint8_t OmfRead8AndAdvance(struct open_mapped_file* omf) {
    uint8_t rv = OmfRead8(omf);
    OmfAdvance(omf, 1);
    return rv;
}

export uint16_t OmfRead16AndAdvanceLE(struct open_mapped_file* omf) {
    uint16_t rv = OmfRead16LE(omf);
    OmfAdvance(omf, 2);
    return rv;
}

export uint32_t OmfRead32AndAdvanceLE(struct open_mapped_file* omf) {
    uint32_t rv = OmfRead32LE(omf);
    OmfAdvance(omf, 4);
    return rv;
}


export uint16_t OmfRead16BE(struct open_mapped_file* omf) {
    if (omf->error || omf->len < 2 || (omf->cursor > omf->len - 2)) {
        omf->error = true;
        return 0;
    }
    return ReadUnaligned16BE(AddVoidPtr(omf->ptr, omf->cursor));
}

export uint32_t OmfRead32BE(struct open_mapped_file* omf) {
    if (omf->error || omf->len < 4 || (omf->cursor > omf->len - 4)) {
        omf->error = true;
        return 0;
    }
    return ReadUnaligned32BE(AddVoidPtr(omf->ptr, omf->cursor));
}

export uint16_t OmfRead16AndAdvanceBE(struct open_mapped_file* omf) {
    uint16_t rv = OmfRead16BE(omf);
    OmfAdvance(omf, 2);
    return rv;
}

export uint32_t OmfRead32AndAdvanceBE(struct open_mapped_file* omf) {
    uint32_t rv = OmfRead32BE(omf);
    OmfAdvance(omf, 4);
    return rv;
}