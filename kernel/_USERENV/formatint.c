#include <common.h>
#include "format.h"
#include <errno.h>

export pageableuserexec uint64_t UInt64Div10(uint64_t n, int* rem) {
    uint64_t q, r;
    q = (n >> 1) + (n >> 2);
    q = q + (q >> 4);
    q = q + (q >> 8);
    q = q + (q >> 16);
    q = q + (q >> 32);
    q = q >> 3;
    r = n - (((q << 2) + q) << 1); 
    if (r > 9) {
        q += 1;
        r -= 10;
    }
    if (rem != NULL) {
        *rem = (int) r;
    }
    return q;
}

export pageableuserexec ssize_t FormatInt(char* out, size_t max, uint64_t v, int base) {
    if (out == NULL || max < 2) {
        return -EINVAL;
    }
    if (v == 0) {
        out[0] = '0';
        out[1] = 0;
        return 1;
    }
    const char digits[16] = "0123456789ABCDEF";
    size_t i = 0;
    if (base == 10) {
        while (v > 0 && i < max - 1) {
            int rem;
            v = UInt64Div10(v, &rem);
            out[i++] = '0' + rem;
        }
    } else if (base == 2 || base == 8 || base == 16) {
        int exponent = base == 16 ? 4 : (base == 8 ? 3 : 1);
        while (v > 0 && i < max - 1) {
            out[i++] = digits[v & ((1 << exponent) - 1)];
            v >>= exponent;
        }
    } else {
        return -ENOTSUP;
    }

    if (v != 0) {
        return -ERANGE;
    }
    out[i] = 0;
    for (size_t j = 0; j < i / 2; ++j) {
        char t = out[j];
        out[j] = out[i - j - 1];
        out[i - j - 1] = t;
    }
    return i;
}