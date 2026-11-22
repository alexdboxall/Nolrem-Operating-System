#pragma once

#include <arch.h>

void KeHandleInterrupt(int num, irqcontext_t ctxt);
void KeRegisterInterruptHandler(int num, void(*handler)(irqcontext_t));

void KeAcquireIrqPrevent(void);
void KeReleaseIrqPrevent(void);