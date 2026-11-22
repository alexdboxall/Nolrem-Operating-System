#include <stdint.h>
#include <string.h>
#include <log.h>
#include <kgfx.h>
#include <common.h>
#include <stdarg.h>

#define PORT 0x3F8

static inline void outb(unsigned short port, unsigned char val) {
    asm volatile("outb %0, %1" : : "a"(val), "Nd"(port));
}

static inline unsigned char inb(unsigned short port) {
    unsigned char ret;
    asm volatile("inb %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}

static void KeLogCharacter(char c) {
    static bool disp_mode = false;
    if (c == 0x01) {
        disp_mode = true;
    } else if ((uint8_t) c == 0x02) { 
        disp_mode = false;
    } else {
        while ((inb(PORT + 5) & 0x20) == 0) {
            ;
        }
        outb(PORT, c);
        if (disp_mode) {
            KeDisplayLog(c);
        }
    }   
}

static void KeLogInt(int i) {
    if (i < 0) {
        KeLogCharacter('-');
        i = -i;
    }
    char buffer[16];
    int digits = 0;
    do {
        buffer[digits++] = i % 10 + '0';
        i /= 10;
    } while (i);
    while (digits--) {
        KeLogCharacter(buffer[digits]);
    }
}

static void KeLogHex(size_t hx) {
    static char hexdigits[] = "0123456789ABCDEF";
    int nibbles = sizeof(size_t) * 2;
    for (int i = 0; i < nibbles; ++i) {
        int nibble = (hx >> (nibbles * 4 - 4 - i * 4)) & 0xF;
        if (i == 8) KeLogCharacter('\'');
        KeLogCharacter(hexdigits[nibble]);
    }
}

static void KeLogString(char* s) {
    while (*s) {
        KeLogCharacter(*s);
        ++s;
    }
}

export void KeLogPrintf(const char* format, ...) {
    va_list args;
    va_start(args, format);

    for (const char* p = format; *p != '\0'; p++) {
        if (*p != '%') {
            KeLogCharacter(*p);
            continue;
        }

        p++; 

        if (*p == '\0') {
            KeLogCharacter('%');
            break;
        }

        switch (*p) {
            case 'd': {
                int i = va_arg(args, int);
                KeLogInt(i);
                break;
            }
            case 's': {
                char* s = va_arg(args, char*);
                KeLogString(s);
                break;
            }
            case 'c': {
                char c = (char)va_arg(args, int);
                KeLogCharacter(c);
                break;
            }
            case 'X': 
            case 'x': {
                size_t hx = va_arg(args, size_t);
                KeLogHex(hx);
                break;
            }
            case '%': {
                KeLogCharacter('%');
                break;
            }
            default: {
                KeLogCharacter('%');
                KeLogCharacter(*p);
                break;
            }
        }
    }

    va_end(args);
}

void KeInitLog(void) {
    uint16_t* vram = (uint16_t*) (0xC0000000 + 0xB8000);
    memset(vram, 0, 2 * 25 * 80);

    outb(PORT + 1, 0x00);    // Disable all interrupts
    outb(PORT + 3, 0x80);    // Enable DLAB (set baud rate divisor)
    outb(PORT + 0, 0x03);    // Set divisor to 3 (lo byte) -> 38400 baud
    outb(PORT + 1, 0x00);    //                  (hi byte)
    outb(PORT + 3, 0x03);    // 8 bits, no parity, one stop bit (8N1)
    outb(PORT + 2, 0xC7);    // Enable FIFO, clear them, with 14-byte threshold
    outb(PORT + 4, 0x0B);    // IRQs enabled, RTS/DSR set

    KeLogPrintf("Kernel started...\n");
}

