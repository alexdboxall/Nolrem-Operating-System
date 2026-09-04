#include <common.h>
#include <log.h>
#include <stdint.h>
#include <stddef.h>
#include <CLIPDRAW/gfx_driver.h>

static struct graphics_driver kgfx_driver = {0};

void RegisterPrimaryGraphicsDriver(struct graphics_driver drv) {
    kgfx_driver = drv;
}

struct graphics_driver* GetKernelGraphicsDriver(void) {
    return &kgfx_driver;
}

void KernelDisplayPanic(const char* s) {
    if (kgfx_driver.panic != NULL) {
        kgfx_driver.panic(s);
    }
}

static int cx = 0;
static int cy = 0;
void KernelDisplayLog(char c) {
    uint16_t* vram = (uint16_t*) (size_t) 0xC00B8000;
    if (c != '\n') {
        vram += cy * 80 + cx;
        *vram = 0x0700 | c;
    }
    cx++;
    if (cx == 80 || c == '\n') {
        cx = 0;
        cy = (cy + 1) % 25;
    }
    if (kgfx_driver.log != NULL) {
        kgfx_driver.log(c);
    }
}