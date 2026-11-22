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
void OmfClose(struct open_mapped_file* omf); 

void OmfAdvance(struct open_mapped_file* omf, size_t amount);
void OmfJump(struct open_mapped_file* omf, size_t offset);
uint8_t OmfRead8(struct open_mapped_file* omf);
uint8_t OmfRead8AndAdvance(struct open_mapped_file* omf);

uint16_t OmfRead16BE(struct open_mapped_file* omf);
uint32_t OmfRead32BE(struct open_mapped_file* omf);
uint16_t OmfRead16AndAdvanceBE(struct open_mapped_file* omf);
uint32_t OmfRead32AndAdvanceBE(struct open_mapped_file* omf);
uint16_t OmfRead16LE(struct open_mapped_file* omf);
uint32_t OmfRead32LE(struct open_mapped_file* omf);
uint16_t OmfRead16AndAdvanceLE(struct open_mapped_file* omf);
uint32_t OmfRead32AndAdvanceLE(struct open_mapped_file* omf);