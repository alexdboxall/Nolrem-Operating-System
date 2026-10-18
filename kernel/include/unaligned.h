#pragma once

#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <common.h>

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
