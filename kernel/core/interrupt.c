#include <arch.h>
#include <common.h>

static void(*irq_handlers[16])(irqcontext_t) = {0};

void HandleInterrupt(int num, irqcontext_t ctxt) {
    if (irq_handlers[num] != NULL) {
        irq_handlers[num](ctxt);
    }
}

void RegisterInterruptHandler(int num, void(*handler)(irqcontext_t)) {
    irq_handlers[num] = handler;
}