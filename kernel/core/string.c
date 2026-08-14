
#include <string.h>
#include <errno.h>
#include <stdint.h>
#include <stdbool.h>
#include <ctype.h>

/*
 * Contains the code for memcpy, memset, strcpy, strcmp and strlen.
 */
#include <cmn_string.h>
#include <common.h>

export userexec void* memchr(const void* s, int c, size_t n) {
	const uint8_t* ptr = (const uint8_t*) s;

	while (n--) {
		if (*ptr == (uint8_t) c) {
			return (void*) ptr;
		}

		++ptr;
	}

	return NULL;
}

export userexec int memcmp(const void* s1, const void* s2, size_t n) {
    const uint8_t* a = (const uint8_t*) s1;
	const uint8_t* b = (const uint8_t*) s2;

	for (size_t i = 0; i < n; ++i) {
		if (a[i] < b[i]) return -1;
		else if (a[i] > b[i]) return 1;
	}
	
	return 0;
}

export userexec int strncmp(const char* s1, const char* s2, size_t n) {
	while (n && *s1 && (*s1 == *s2)) {
		++s1;
		++s2;
		--n;
	}
	if (n == 0) {
		return 0;
	} else {
		return (*(unsigned char*) s1 - *(unsigned char*) s2);
	}
}

export userexec void* memmove(void* dst, const void* src, size_t n) {
	uint8_t* a = (uint8_t*) dst;
	const uint8_t* b = (const uint8_t*) src;

	if (a <= b) {
		while (n--) {
			*a++ = *b++;
		}
	} else {
		b += n;
		a += n;

		while (n--) {
			*--a = *--b;
		}
	}

	return dst;
}

export userexec char* strcat(char* restrict dst, const char* restrict src) {
	char* ret = dst;

	while (*dst) {
		++dst;
	}

	while ((*dst++ = *src++)) {
		;
	}

	return ret;
}

export userexec char* strncpy(char* restrict dst, const char* restrict src, size_t n) {
	char* ret = dst;

	while (n--) {
		if (*src) {
			*dst++ = *src++;
		} else {
			*dst++ = 0;
		}
	}

	return ret;
}

export userexec char* strncat(char* restrict dst, const char* restrict src, size_t n) {
	char* ret = dst;

	while (*dst) {
		++dst;
	}

	while (*src && n--) {
		*dst++ = *src++;
	}	

	*dst = 0;
	return ret;
}
