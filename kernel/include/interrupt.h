#pragma once

#include <arch.h>

void HandleInterrupt(int num, irqcontext_t ctxt);
void RegisterInterruptHandler(int num, void(*handler)(irqcontext_t));