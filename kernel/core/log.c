#include <stdint.h>
#include <string.h>
#include <log.h>
#include <kgfx.h>

void LogInt(int i) {
    if (i < 0) {
        LogCharacter('-');
        i = -i;
    }
    char buffer[16];
    int digits = 0;
    do {
        buffer[digits++] = i % 10 + '0';
        i /= 10;
    } while (i);
    while (digits--) {
        LogCharacter(buffer[digits]);
    }
}

void LogHex(size_t hx) {
    static char hexdigits[] = "0123456789ABCDEF";
    int nibbles = sizeof(size_t) * 2;
    for (int i = 0; i < nibbles; ++i) {
        int nibble = (hx >> (nibbles * 4 - 4 - i * 4)) & 0xF;
        if (i == 8) LogCharacter('\'');
        LogCharacter(hexdigits[nibble]);
    }
}

void LogStringAndHexLine(char* s, size_t hx) {
    LogString(s);
    LogHex(hx);
    LogCharacter('\n');
}

void LogCharacter(char c) {
    KernelDisplayLog(c);
}

void LogString(char* s) {
    while (*s) {
        LogCharacter(*s);
        ++s;
    }
}

void InitLog(void) {
    uint16_t* vram = (uint16_t*) (0xC0000000 + 0xB8000);
    memset(vram, 0, 2 * 25 * 80);
    LogString("Kernel started...\n");
}
