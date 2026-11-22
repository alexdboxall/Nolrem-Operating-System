#include <common.h>
#include <arch.h>
#include <cpu.h>
#include <panic.h>
#include <log.h>

void KeAcquireIrqPrevent(void) {
    bool were_irqs_on = ArchAreInterruptsEnabled();
    ArchDisableInterrupts();
    struct cpu_data* cpu = KeGetCpu();
    if (cpu->irq_disable_counter++ == 0) {
        cpu->saved_interrupt_enabled = were_irqs_on;
    }
//#ifndef NDEBUG
    if (cpu->irq_disable_counter == 0) {
        KeLogPrintf("irq_disable_counter was -1\n");
        KePanic(PANIC_FUCK_ME);
    }
//#endif
}

void KeReleaseIrqPrevent(void) {
    struct cpu_data* cpu = KeGetCpu();
//#ifndef NDEBUG
    if (cpu->irq_disable_counter == 0) {
        KeLogPrintf("irq_disable_counter will be -1\n");
        KePanic(PANIC_FUCK_ME);
    }
//#endif
    if (--cpu->irq_disable_counter == 0) {
        if (cpu->saved_interrupt_enabled) {
            ArchEnableInterrupts();
        }
    }
}