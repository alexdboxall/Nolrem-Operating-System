#pragma once

#include <stdint.h>
#include <stddef.h>

#include <CLIPDRAW/gfx_driver.h>

void RegisterPrimaryGraphicsDriver(struct graphics_driver drv);
void KernelDisplayPanic(const char* s);
void KernelDisplayLog(char c);
struct graphics_driver* GetKernelGraphicsDriver(void);