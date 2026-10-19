#pragma once

#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <common.h>
#include <endian.h>

static inline uint8_t ReadUnaligned8(void* ptr) {
    return *((uint8_t*) ptr);
}

static inline uint16_t ReadUnaligned16(void* ptr) {
    uint16_t val;
    __builtin_memcpy(&val, ptr, sizeof(val));
    return val;
}

static inline uint32_t ReadUnaligned32(void* ptr) {
    uint32_t val;
    __builtin_memcpy(&val, ptr, sizeof(val));
    return val;
}

static inline uint64_t ReadUnaligned64(void* ptr) {
    uint64_t val;
    __builtin_memcpy(&val, ptr, sizeof(val));
    return val;
}

static inline size_t ReadUnalignedSizeT(void* ptr) {
    size_t val;
    __builtin_memcpy(&val, ptr, sizeof(val));
    return val;
}

static inline uint8_t ReadUnaligned8AndAdvance(void** ptr) {
    uint8_t val = ReadUnaligned8(*ptr);
    *ptr = AddVoidPtr(*ptr, 1);
    return val;
}

static inline uint16_t ReadUnaligned16AndAdvance(void** ptr) {
    uint16_t val;
    __builtin_memcpy(&val, *ptr, sizeof(val));
    *ptr = AddVoidPtr(*ptr, sizeof(val));
    return val;
}

static inline uint32_t ReadUnaligned32AndAdvance(void** ptr) {
    uint32_t val;
    __builtin_memcpy(&val, *ptr, sizeof(val));
    *ptr = AddVoidPtr(*ptr, sizeof(val));
    return val;
}

static inline uint64_t ReadUnaligned64AndAdvance(void** ptr) {
    uint64_t val;
    __builtin_memcpy(&val, *ptr, sizeof(val));
    *ptr = AddVoidPtr(*ptr, sizeof(val));
    return val;
}

static inline size_t ReadUnalignedSizeTAndAdvance(void** ptr) {
    size_t val;
    __builtin_memcpy(&val, *ptr, sizeof(val));
    *ptr = AddVoidPtr(*ptr, sizeof(val));
    return val;
}

static inline uint8_t ReadUnaligned8LE(void* ptr) {
    return ReadUnaligned8(ptr);
}

static inline uint16_t ReadUnaligned16LE(void* ptr) {
    uint8_t* p = (uint8_t*) ptr;
    return (uint16_t) ((uint16_t) p[0] | ((uint16_t) p[1] << 8));
}

static inline uint32_t ReadUnaligned32LE(void* ptr) {
    uint8_t* p = (uint8_t*) ptr;
    return ((uint32_t) p[0])
         | ((uint32_t) p[1] << 8)
         | ((uint32_t) p[2] << 16)
         | ((uint32_t) p[3] << 24);
}

static inline uint64_t ReadUnaligned64LE(void* ptr) {
    uint8_t* p = (uint8_t*) ptr;
    return ((uint64_t) p[0])
         | ((uint64_t) p[1] << 8)
         | ((uint64_t) p[2] << 16)
         | ((uint64_t) p[3] << 24)
         | ((uint64_t) p[4] << 32)
         | ((uint64_t) p[5] << 40)
         | ((uint64_t) p[6] << 48)
         | ((uint64_t) p[7] << 56);
}

static inline size_t ReadUnalignedSizeTLE(void* ptr) {
    if (sizeof(size_t) == 8) {
        return (size_t) ReadUnaligned64LE(ptr);
    }
    return (size_t) ReadUnaligned32LE(ptr);
}

static inline uint8_t ReadUnaligned8AndAdvanceLE(void** ptr) {
    uint8_t val = ReadUnaligned8LE(*ptr);
    *ptr = AddVoidPtr(*ptr, sizeof(val));
    return val;
}

static inline uint16_t ReadUnaligned16AndAdvanceLE(void** ptr) {
    uint16_t val = ReadUnaligned16LE(*ptr);
    *ptr = AddVoidPtr(*ptr, sizeof(val));
    return val;
}

static inline uint32_t ReadUnaligned32AndAdvanceLE(void** ptr) {
    uint32_t val = ReadUnaligned32LE(*ptr);
    *ptr = AddVoidPtr(*ptr, sizeof(val));
    return val;
}

static inline uint64_t ReadUnaligned64AndAdvanceLE(void** ptr) {
    uint64_t val = ReadUnaligned64LE(*ptr);
    *ptr = AddVoidPtr(*ptr, sizeof(val));
    return val;
}

static inline size_t ReadUnalignedSizeTAndAdvanceLE(void** ptr) {
    size_t val = ReadUnalignedSizeTLE(*ptr);
    *ptr = AddVoidPtr(*ptr, sizeof(val));
    return val;
}

/* ------------------------------------------------------------------------ *
 * Big-endian readers
 * ------------------------------------------------------------------------ */

static inline uint8_t ReadUnaligned8BE(void* ptr) {
    return ReadUnaligned8(ptr);
}

static inline uint16_t ReadUnaligned16BE(void* ptr) {
    uint8_t* p = (uint8_t*) ptr;
    return (uint16_t) (((uint16_t) p[0] << 8) | (uint16_t) p[1]);
}

static inline uint32_t ReadUnaligned32BE(void* ptr) {
    uint8_t* p = (uint8_t*) ptr;
    return ((uint32_t) p[0] << 24)
         | ((uint32_t) p[1] << 16)
         | ((uint32_t) p[2] << 8)
         | ((uint32_t) p[3]);
}

static inline uint64_t ReadUnaligned64BE(void* ptr) {
    uint8_t* p = (uint8_t*) ptr;
    return ((uint64_t) p[0] << 56)
         | ((uint64_t) p[1] << 48)
         | ((uint64_t) p[2] << 40)
         | ((uint64_t) p[3] << 32)
         | ((uint64_t) p[4] << 24)
         | ((uint64_t) p[5] << 16)
         | ((uint64_t) p[6] << 8)
         | ((uint64_t) p[7]);
}

static inline size_t ReadUnalignedSizeTBE(void* ptr) {
    if (sizeof(size_t) == 8) {
        return (size_t) ReadUnaligned64BE(ptr);
    }
    return (size_t) ReadUnaligned32BE(ptr);
}

static inline uint8_t ReadUnaligned8AndAdvanceBE(void** ptr) {
    uint8_t val = ReadUnaligned8BE(*ptr);
    *ptr = AddVoidPtr(*ptr, sizeof(val));
    return val;
}

static inline uint16_t ReadUnaligned16AndAdvanceBE(void** ptr) {
    uint16_t val = ReadUnaligned16BE(*ptr);
    *ptr = AddVoidPtr(*ptr, sizeof(val));
    return val;
}

static inline uint32_t ReadUnaligned32AndAdvanceBE(void** ptr) {
    uint32_t val = ReadUnaligned32BE(*ptr);
    *ptr = AddVoidPtr(*ptr, sizeof(val));
    return val;
}

static inline uint64_t ReadUnaligned64AndAdvanceBE(void** ptr) {
    uint64_t val = ReadUnaligned64BE(*ptr);
    *ptr = AddVoidPtr(*ptr, sizeof(val));
    return val;
}

static inline size_t ReadUnalignedSizeTAndAdvanceBE(void** ptr) {
    size_t val = ReadUnalignedSizeTBE(*ptr);
    *ptr = AddVoidPtr(*ptr, sizeof(val));
    return val;
}