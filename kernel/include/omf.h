#pragma once

struct cpz_image;

struct open_mapped_file {
    struct file* f;
    struct cpz_image* cpz;
    void* ptr;
    off_t len;
    bool error;
    off_t cursor;
};

#define OMF_TRANSPARENT_DECOMPRESS  (1 << 0)

int KeOpenAndMapFile(const char* path, int flags, struct open_mapped_file* out);

/* DOUBLE CLOSE IS ALLOWED! */
void KeOmfClose(struct open_mapped_file* omf); 

void KeOmfAdvance(struct open_mapped_file* omf, size_t amount);
void KeOmfJump(struct open_mapped_file* omf, size_t offset);
uint8_t KeOmfRead8(struct open_mapped_file* omf);
uint8_t KeOmfRead8AndAdvance(struct open_mapped_file* omf);

uint16_t KeOmfRead16BE(struct open_mapped_file* omf);
uint32_t KeOmfRead32BE(struct open_mapped_file* omf);
uint16_t KeOmfRead16AndAdvanceBE(struct open_mapped_file* omf);
uint32_t KeOmfRead32AndAdvanceBE(struct open_mapped_file* omf);
uint16_t KeOmfRead16LE(struct open_mapped_file* omf);
uint32_t KeOmfRead32LE(struct open_mapped_file* omf);
uint16_t KeOmfRead16AndAdvanceLE(struct open_mapped_file* omf);
uint32_t KeOmfRead32AndAdvanceLE(struct open_mapped_file* omf);