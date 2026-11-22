#pragma once

#include <stdint.h>
#include <stddef.h>

#include <CLIPDRAW/gfx_driver.h>

void KeRegisterPrimaryGraphicsDriver(struct graphics_driver drv);
void KeDisplayPanic(const char* s);
void KeDisplayLog(char c);
struct graphics_driver* KeGetKernelGraphicsDriver(void);