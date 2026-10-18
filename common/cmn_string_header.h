#pragma once

#undef USE_BUILTIN_MEMCPY
#undef USE_BUILTIN_MEMSET

#ifdef USE_BUILTIN_MEMCPY
#define memcpy(dst, src, n) __builtin_memcpy(dst, src, n)
#else
//void* memcpy(void* restrict dst, const void* restrict src, size_t n);
static inline void* memcpy(void* restrict dst, const void* restrict src, size_t n)
{
	uint8_t* a = (uint8_t*) dst;
	const uint8_t* b = (const uint8_t*) src;

	for (size_t i = 0; i < n; ++i) {
		a[i] = b[i];
	}

	return dst;
}
#endif

#ifdef USE_BUILTIN_MEMSET
#define memset(dst, c, n) __builtin_memset(dst, c, n)
#else
//void* memset(void* addr, int c, size_t n);
__attribute__((always_inline)) static inline void* memset(void* addr, int c, size_t n)
{
	uint8_t* ptr = (uint8_t*) addr;
	for (size_t i = 0; i < n; ++i) {
		ptr[i] = c;
	}

	return addr;
}
#endif
