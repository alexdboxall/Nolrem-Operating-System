#include <common.h>
#include <log.h>
#include <stdint.h>
#include <stddef.h>
#include <CLIPDRAW/gfx_driver.h>

static struct graphics_driver kgfx_driver = {0};

export void KeRegisterPrimaryGraphicsDriver(struct graphics_driver drv) {
    kgfx_driver = drv;
}

export struct graphics_driver* KeGetKernelGraphicsDriver(void) {
    return &kgfx_driver;
}

void KeDisplayPanic(const char* s) {
    if (kgfx_driver.panic != NULL) {
        kgfx_driver.panic(s);
    }
}

void KeDisplayLog(char c) {
    if (kgfx_driver.log != NULL) {
        kgfx_driver.log(c);
    }
}