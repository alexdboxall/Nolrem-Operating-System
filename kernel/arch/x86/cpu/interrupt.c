
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

static size_t GetCr2(void) {
    size_t val;
    __asm__ __volatile__("mov %%cr2, %0" : "=r" (val) :: );
    return val;
}

void x86HandleInterrupt(struct x86_regs* r) {
    int num = r->int_no;

    if (num == ISR_SYSTEM_CALL) {
        LogStringAndHexLine("Handling system call number 0x", r->eax);
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

        LogStringAndHexLine("Page fault: EIP 0x", r->eip);
        LogStringAndHexLine("            CR2 0x", cr2);
        LogStringAndHexLine("         REASON 0x", reason);

        HandlePageFault(cr2, reason);
        return;
    }

    if (num < PIC_IRQ_BASE) {
        LogStringAndHexLine("Interrupt 0x", num);
        LogStringAndHexLine("EIP = ", r->eip);
        while (true) {
            ;
        }
    }
    
    if (num >= PIC_IRQ_BASE && num < PIC_IRQ_BASE + 16) {
        HandleInterrupt(num - PIC_IRQ_BASE, r);
        SendPicEoi(num);
    }

    ProcessIrqPostMessage();    
}

