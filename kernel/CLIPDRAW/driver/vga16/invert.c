#include "vga16.h"

#define VGA_W       640
#define VGA_H       480
#define VGA_STRIDE  80
#define VGA_BASE    ((volatile uint8_t*) 0xC00A0000)

static inline void XorMasked(volatile uint8_t* p) {
    uint8_t d = *p; (void) d;
    *p = 0xFF;
}

/*
 * One byte column, `geom` giving the horizontal clip within the byte.
 * When `half`, splits into two passes so each gets a constant bit mask.
 */
static void XorColumn(int byte_x, int y1, int y2, uint8_t geom, bool half) {
    int phases = half ? 2 : 1;

    for (int phase = 0; phase < phases; phase++) {
        uint8_t mask = geom;
        int ys = y1;
        int step = 1;

        if (half) {
            mask &= (phase ? 0x55 : 0xAA);
            /* phase 0 -> even scanlines, phase 1 -> odd */
            ys = y1 + (((y1 & 1) == phase) ? 0 : 1);
            step = 2;
        }

        if (mask == 0) continue;    /* nothing of this phase inside the clip */

        outb(0x3CF, mask);

        volatile uint8_t* p = VGA_BASE + ys * VGA_STRIDE + byte_x;
        for (int y = ys; y < y2; y += step, p += VGA_STRIDE * step) {
            XorMasked(p);
        }
    }
}

/* Fully-covered byte columns byte_a..byte_b inclusive. */
static void XorMiddle(int byte_a, int byte_b, int y1, int y2, bool half) {
    int n = byte_b - byte_a + 1;
    if (n <= 0) return;

    int phases = half ? 2 : 1;

    for (int phase = 0; phase < phases; phase++) {
        uint8_t mask = half ? (phase ? 0x55 : 0xAA) : 0xFF;
        int ys   = half ? (y1 + (((y1 & 1) == phase) ? 0 : 1)) : y1;
        int step = half ? 2 : 1;

        outb(0x3CF, mask);

        volatile uint8_t* row = VGA_BASE + ys * VGA_STRIDE + byte_a;
        for (int y = ys; y < y2; y += step, row += VGA_STRIDE * step) {
            volatile uint8_t* q = row;
            int m = n;
            while (m >= 4) {
                XorMasked(q + 0);
                XorMasked(q + 1);
                XorMasked(q + 2);
                XorMasked(q + 3);
                q += 4;
                m -= 4;
            }
            while (m--) {
                XorMasked(q++);
            }
        }
    }
}

/*
 * Assumes VgaBeginXor() has been called and the GC index register is
 * parked on 0x08. Use this one when inverting many rects in a batch.
 */
void VgaInvertRectFast(int x1, int y1, int x2, int y2, bool half) {
    if (x1 < 0) x1 = 0;
    if (y1 < 0) y1 = 0;
    if (x2 > VGA_W) x2 = VGA_W;
    if (y2 > VGA_H) y2 = VGA_H;
    if (x1 >= x2 || y1 >= y2) return;

    int byte_x1 = x1 >> 3;
    int byte_x2 = (x2 - 1) >> 3;

    uint8_t mask_start = (uint8_t) (0xFF >> (x1 & 7));
    uint8_t mask_end   = (uint8_t) (0xFF << (7 - ((x2 - 1) & 7)));

    if (byte_x1 == byte_x2) {
        XorColumn(byte_x1, y1, y2, mask_start & mask_end, half);
        return;
    }

    XorColumn(byte_x1, y1, y2, mask_start, half);
    XorMiddle(byte_x1 + 1, byte_x2 - 1, y1, y2, half);
    XorColumn(byte_x2, y1, y2, mask_end, half);
}

void VgaBeginXor(void) {
    outb(0x3C4, 0x02); outb(0x3C5, 0x0F);   /* map mask: all four planes */
    outb(0x3CE, 0x01); outb(0x3CF, 0x00);   /* enable set/reset off      */
    outb(0x3CE, 0x03); outb(0x3CF, 0x18);   /* XOR, rotate 0             */
    outb(0x3CE, 0x08);                      /* park index on bit mask    */
}

void VgaEndXor(void) {
    outb(0x3CF, 0xFF);                      /* index still on 0x08 */
    outb(0x3CE, 0x03); outb(0x3CF, 0x00);
}

void VgaInvertRect(struct graphics_driver* drv, int x1, int y1, int x2, int y2, bool half) {
    (void) drv;
    VgaBeginXor();
    VgaInvertRectFast(x1, y1, x2, y2, half);
    VgaEndXor();
}