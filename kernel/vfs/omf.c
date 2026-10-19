#include <common.h>
#include <vmm.h>
#include <vfs.h>
#include <fcntl.h>
#include <omf.h>
#include <errno.h>
#include <unaligned.h>

export int OpenAndMapFile(const char* path, struct open_mapped_file* out) {
    if (out == NULL) {
        return EINVAL;
    }
    out->f = NULL;
    out->error = true;
    out->ptr = NULL;
    out->cursor = 0;
    out->len = 0;
    if (path == NULL) {
        return EINVAL;
    }
    int res = OpenFile(path, O_RDONLY, 0, &out->f);
    if (res != 0) {
        out->f = NULL;
        return res;
    }
    off_t full_size = out->f->node->stat.st_size;
    size_t len = (size_t) full_size;
    if (full_size <= 0 || (uintmax_t) len != (uintmax_t) full_size) {
        DerefObject(out->f);
        out->f = NULL;
        return EINVAL;
    }
    /* TODO: we also want to lock the file so that no-one can modify it under
     * us if a part of it gets paged out. */
    out->len = len;
    out->ptr = AllocFileMemory(out->f, 0, out->len, 0, 0);
    if (out->ptr == NULL) {
        DerefObject(out->f);
        out->f = NULL;
        return ENOMEM;
    }
    out->error = false;
    return 0;
}

/* DOUBLE CLOSE IS ALLOWED! */
export void OmfClose(struct open_mapped_file* omf) {
    if (omf == NULL) {
        return;
    }
    if (omf->ptr != NULL) {
        FreeMemory(omf->ptr, omf->len);
        omf->ptr = NULL;
    }
    if (omf->f != NULL) {
        DerefObject(omf->f);
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
