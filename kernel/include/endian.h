#pragma once

#define LITTLE_ENDIAN 0
#define BIG_ENDIAN    1

#if defined(__BYTE_ORDER__) && defined(__ORDER_LITTLE_ENDIAN__)
    #if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
        #define TARGET_ENDIAN LITTLE_ENDIAN
    #elif __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
        #define TARGET_ENDIAN BIG_ENDIAN
    #endif
#elif defined(__LITTLE_ENDIAN__)
    #define TARGET_ENDIAN LITTLE_ENDIAN
#elif defined(__BIG_ENDIAN__)
    #define TARGET_ENDIAN BIG_ENDIAN
#elif defined(_WIN32) || defined(_WIN64)
    #define TARGET_ENDIAN LITTLE_ENDIAN
#elif defined(__i386__) || defined(__x86_64__) || defined(_M_IX86) || defined(_M_X64)
    #define TARGET_ENDIAN LITTLE_ENDIAN
#elif defined(__ARMEB__) || defined(__THUMBEB__) || defined(__AARCH64EB__) || defined(_MIPSEB)
    #define TARGET_ENDIAN BIG_ENDIAN
#elif defined(__ARMEL__) || defined(__THUMBEL__) || defined(__AARCH64EL__) || defined(_MIPSEL)
    #define TARGET_ENDIAN LITTLE_ENDIAN
#else
    #error "Could not determine target endianness at compile time."
#endif

static inline uint16_t Le16(const uint8_t* p) {
    return (uint16_t) (p[0] | (p[1] << 8));
}

static inline uint32_t Le32(const uint8_t* p) {
    return (uint32_t) p[0] | ((uint32_t) p[1] << 8) | ((uint32_t) p[2] << 16) | ((uint32_t) p[3] << 24);
}
