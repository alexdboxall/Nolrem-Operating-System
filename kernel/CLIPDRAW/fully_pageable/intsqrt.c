#include <common.h>

export pageable int IntegerSqrt(int x) {
    if (x <= 0) {
        return 0;
    }
    if (x <= 2) {
        return 1;
    }
    int r = 0;
    int bit = 1 << 30;

    while (bit > x) bit >>= 2;

    while (bit != 0) {
        if (x >= r + bit) {
            x -= r + bit;
            r = (r >> 1) + bit;
        } else {
            r >>= 1;
        }
        bit >>= 2;
    }
    return r;
}