#pragma once

#define LITTLE_ENDIAN 0
#define BIG_ENDIAN    1

/* 1. GCC and Clang natively expose __BYTE_ORDER__ */
#if defined(__BYTE_ORDER__) && defined(__ORDER_LITTLE_ENDIAN__)
    #if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
        #define TARGET_ENDIAN LITTLE_ENDIAN
    #elif __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
        #define TARGET_ENDIAN BIG_ENDIAN
    #endif

/* 2. Alternative architectural macros for GCC/Clang */
#elif defined(__LITTLE_ENDIAN__)
    #define TARGET_ENDIAN LITTLE_ENDIAN
#elif defined(__BIG_ENDIAN__)
    #define TARGET_ENDIAN BIG_ENDIAN

/* 3. Windows / MSVC targets (Almost exclusively Little Endian) */
#elif defined(_WIN32) || defined(_WIN64)
    #define TARGET_ENDIAN LITTLE_ENDIAN

/* 4. Common specific architecture flags */
#elif defined(__i386__) || defined(__x86_64__) || defined(_M_IX86) || defined(_M_X64)
    #define TARGET_ENDIAN LITTLE_ENDIAN
#elif defined(__ARMEB__) || defined(__THUMBEB__) || defined(__AARCH64EB__) || defined(_MIPSEB)
    #define TARGET_ENDIAN BIG_ENDIAN
#elif defined(__ARMEL__) || defined(__THUMBEL__) || defined(__AARCH64EL__) || defined(_MIPSEL)
    #define TARGET_ENDIAN LITTLE_ENDIAN

#else
    #error "Could not determine target endianness at compile time."
#endif
