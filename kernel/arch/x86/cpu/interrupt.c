
#include <common.h>
#include <log.h>
#include <machine/x86.h>
#include <interrupt.h>
#include <vmm.h>
#include <syscall.h>
#include <scheduler.h>

#define ISR_SYSTEM_CALL 96
#define ISR_PAGE_FAULT  14
#define ISR_NMI         2
#define ISR_INVALID_OP  6
#define ISR_DIV_ERR     0

bool ArchAreInterruptsEnabled(void) {
    unsigned long flags;
    asm volatile ( "pushf\n\t"
                   "pop %0"
                   : "=g"(flags) );
    return flags & (1 << 9);
}

void ArchEnableInterrupts(void) {
    asm volatile ("sti");
}

void ArchDisableInterrupts(void) {
    asm volatile ("cli");
}

static size_t GetCr2(void) {
    size_t val;
    __asm__ __volatile__("mov %%cr2, %0" : "=r" (val) :: );
    return val;
}

void x86HandleInterrupt(struct x86_regs* r) {
    int num = r->int_no;

    if (num == ISR_SYSTEM_CALL) {
        r->eax = PerformSystemCall(r->eax, r->ebx, r->ecx, r->edx, r->esi);
        return;
    }

    if (num == ISR_PAGE_FAULT) {
        size_t cr2 = GetCr2();
        int reason = 0;
        if (r->err_code & 1) reason |= PF_PRESENT;
        if (r->err_code & 2) reason |= PF_WRITE;
        if (r->err_code & 4) reason |= PF_USER;
        if (r->err_code & 8) reason |= PF_FETCH;

        LogPrintf("Page fault: EIP 0x%X\n", r->eip);
        LogPrintf("            CR2 0x%X\n", cr2);
        LogPrintf("         REASON 0x%X\n", reason);

        HandlePageFault(cr2, reason);
        return;
    }

    if (num < PIC_IRQ_BASE) {
        LogPrintf("Interrupt %d\nEIP = 0x%X", num, r->eip);
        while (true) {
            ;
        }
    }
    
    if (num >= PIC_IRQ_BASE && num < PIC_IRQ_BASE + 16) {
        LogPrintf("IRQ %d\n", num - PIC_IRQ_BASE);
        HandleInterrupt(num - PIC_IRQ_BASE, r);
        SendPicEoi(num);
    }

    ProcessIrqPostMessage();    
}

