#include <stdint.h>
#include <string.h>
#include "../../api.h"

static void AddColour(uint32_t pseduo_valid, int weight, int* weights, bool recursive) {
    uint8_t r = GetRed(pseduo_valid);
    uint8_t g = GetGreen(pseduo_valid);
    uint8_t b = GetBlue(pseduo_valid);

    if (pseduo_valid == 0x808080) {
        weights[8] += weight;

    } else if ((r == 0x80 || r == 0x00) && (g == 0x80 || g == 0x00) && (b == 0x80 || b == 0x00)) {
        int idx = (b == 0x80 ? 4 : 0) + (g == 0x80 ? 2 : 0) + (r == 0x80 ? 1 : 0);
        weights[idx] += weight;
    
    } else if ((r == 0xFF || r == 0x00) && (g == 0xFF || g == 0x00) && (b == 0xFF || b == 0x00)) {
        int idx = 8 + (b == 0xFF ? 4 : 0) + (g == 0xFF ? 2 : 0) + (r == 0xFF ? 1 : 0);
        weights[idx] += weight;
    
    } else if (!recursive) {    // not needed if inputs valid, but prevent malformed inputs from locking us up
        uint32_t lighter = ((r == 0x80 ? 0xFF : r) << 16) | ((g == 0x80 ? 0xFF : g) << 8) | (b == 0x80 ? 0xFF : b);
        uint32_t darker  = ((r == 0x80 ? 0x00 : r) << 16) | ((g == 0x80 ? 0x00 : g) << 8) | (b == 0x80 ? 0x00 : b);
        AddColour(darker , weight - weight / 4, weights, true);
        AddColour(lighter, weight / 4         , weights, true);
    }
}

static int CorrectGamma128(int x) {
    return (x * x) >> 7;
}

static int CorrectGamma64(int x) {
    return (x * x) >> 6;
}

// weight out is between 0-128 inclusive, where 0 means 'all A' and 128 is 'all B'
static void GetColourForChannel(uint8_t channel, uint32_t* a, uint32_t* b, int* weight, int shift) {
    if (channel == 0x00 || channel == 0xFF || channel == 0x80) {
        *a = channel << shift;
        *b = channel << shift;
        *weight = 0;
    } else if (channel < 0x80) {
        *a = 0x00 << shift;
        *b = 0x80 << shift;
        *weight = CorrectGamma128(channel + 1);
    } else {
        *a = 0x80 << shift;
        *b = 0xFF << shift;
        *weight = CorrectGamma128(channel - 0x80 + 1);
    }
}

static void FillDitherUsingWeights(int* vga_weights, uint8_t* outbuffer) {
    const int bayer_inverse[64] = {
        0,  36, 32, 4,  18, 54, 50, 22, 16, 52, 48, 20, 2,  38, 34, 6,
        9,  45, 41, 13, 27, 63, 59, 31, 25, 61, 57, 29, 11, 47, 43, 15,
        8,  44, 40, 12, 26, 62, 58, 30, 24, 60, 56, 28, 10, 46, 42, 14,
        1,  37, 33, 5,  19, 55, 51, 23, 17, 53, 49, 21, 3,  39, 35, 7
    };

    // VGA palette indices ordered strictly from darkest to brightest relative luminance:
    // Y ≈ 0.299R + 0.587G + 0.114B
    static const uint8_t vga_lum_order[16] = {
        0,  // Black          (Y = 0)
        4,  // Dark Blue      (Y ≈ 15)
        12, // Bright Blue    (Y ≈ 29)
        1,  // Dark Red       (Y ≈ 38)
        5,  // Dark Magenta   (Y ≈ 53)
        2,  // Dark Green     (Y ≈ 75)
        9,  // Bright Red     (Y ≈ 76)
        6,  // Dark Cyan      (Y ≈ 90)
        13, // Bright Magenta (Y ≈ 105)
        3,  // Dark Yellow    (Y ≈ 113)
        8,  // Dark Gray      (Y ≈ 128)
        10, // Bright Green   (Y ≈ 150)
        14, // Bright Cyan    (Y ≈ 179)
        7,  // Light Gray     (Y ≈ 192)
        11, // Bright Yellow  (Y ≈ 226)
        15  // White          (Y ≈ 255)
    };

    int index = 0;
    for (int order_idx = 0; order_idx < 16; ++order_idx) {
        int color_idx = vga_lum_order[order_idx];
        int weight = vga_weights[color_idx];

        while (weight-- && index < 64) {
            outbuffer[bayer_inverse[index++]] = color_idx;
        }
    }
}

static void GetGreyDither(uint8_t channel, uint8_t* outbuffer) {
    int weights[16] = {0};

    if (channel == 0xFF) {
        memset(outbuffer, 15, 64);
        return;

    } else if (channel == 0x00) {
        memset(outbuffer, 0, 64);
        return;

    } else if (channel == 0x80) {
        memset(outbuffer, 8, 64);
        return;

    } else if (channel == 0xC0) {
        memset(outbuffer, 7, 64);
        return;

    } else if (channel > 0xC0) {
        weights[7]  = 0x40 - CorrectGamma64((channel - 0xC0));
        weights[15] = CorrectGamma64(channel - 0xC0 + 1);

    } else if (channel > 0x80) {
        weights[7] = CorrectGamma64(channel - 0x80);
        weights[8] = 0x40 - CorrectGamma64((channel - 0x80));

    } else {
        weights[8] = CorrectGamma64(channel / 2);
        weights[0] = 0x40 - CorrectGamma64(channel / 2);
    }

    FillDitherUsingWeights(weights, outbuffer);
}

void NormaliseWeights(int* weights) // in/out, 16 entries, must sum to 64
{
    const int SHIFT = 15;              // 2^15 = 32768; 2,097,152 / 32768 = 64
    const int MASK  = (1 << SHIFT) - 1;

    int rem[16];
    int total = 0;

    for (int i = 0; i < 16; ++i) {
        rem[i] = weights[i] & MASK;    // fractional part being discarded
        weights[i] >>= SHIFT;          // same floor as before
        total += weights[i];
    }

    int deficit = 64 - total;          // how many pixels got lost to flooring (0..15)

    // Hand out the leftover pixels to whichever colours had the largest
    // discarded remainder. deficit <= 15, so this is at most 15 passes
    // over 16 ints - no sort needed.
    for (int n = 0; n < deficit; ++n) {
        int best = 0;
        for (int i = 1; i < 16; ++i) {
            if (rem[i] > rem[best]) {
                best = i;
            }
        }
        weights[best]++;
        rem[best] = -1;                // don't pick this one again
    }
}

// outbuffer just has 8x8 pixel grid, not planar grid
export void GetDither(uint32_t col, uint8_t* outbuffer) {
    int vga_weights[16] = {0};
    uint8_t r = GetRed(col);
    uint8_t g = GetGreen(col);
    uint8_t bl = GetBlue(col);

    bool grey = (r == g && g == bl);
    if (grey) {
        GetGreyDither(r, outbuffer);
        return;
    }

    uint32_t pseduo_colours[8] = {
        0xFF000000, 0xFF000000, 0xFF000000, 0xFF000000, 
        0xFF000000, 0xFF000000, 0xFF000000, 0xFF000000, 
    };
    
    uint32_t a, b;
    int weight;
    int pseduo_weights[16] = {0};

    GetColourForChannel(r, &a, &b, &weight, 16);
    for (int i = 0; i < 8; ++i) {
        pseduo_colours[i] |= (i & 1) ? b : a;
        pseduo_weights[i]  = (i & 1) ? weight : (128 - weight);
    }
    GetColourForChannel(g, &a, &b, &weight, 8);
    for (int i = 0; i < 8; ++i) {
        pseduo_colours[i] |= (i & 2) ? b : a;
        pseduo_weights[i] *= (i & 2) ? weight : (128 - weight);
    }
    GetColourForChannel(bl, &a, &b, &weight, 0);
    for (int i = 0; i < 8; ++i) {
        pseduo_colours[i] |= (i & 4) ? b : a;
        pseduo_weights[i] *= (i & 4) ? weight : (128 - weight);
    }

    for (int i = 0; i < 8; ++i) {
        AddColour(pseduo_colours[i], pseduo_weights[i], vga_weights, false);
    }

    NormaliseWeights(vga_weights);
    FillDitherUsingWeights(vga_weights, outbuffer);
}