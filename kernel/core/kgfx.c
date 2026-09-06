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

void KernelDisplayLog(char c) {
    if (kgfx_driver.log != NULL) {
        kgfx_driver.log(c);
    }
}