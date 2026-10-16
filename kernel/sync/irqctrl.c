#include <common.h>
#include <arch.h>
#include <cpu.h>

void AcquireIrqPrevent(void) {
    bool were_irqs_on = ArchAreInterruptsEnabled();
    ArchDisableInterrupts();
    struct cpu_data* cpu = GetCpu();
    if (cpu->irq_disable_counter++ == 0) {
        cpu->saved_interrupt_enabled = were_irqs_on;
    }
}

void ReleaseIrqPrevent(void) {
    struct cpu_data* cpu = GetCpu();
    if (--cpu->irq_disable_counter == 0) {
        if (cpu->saved_interrupt_enabled) {
            ArchEnableInterrupts();
        }
    }
}