#include <common.h>
#include <machine/x86.h>
#include <log.h>

#define PIC1_COMMAND    0x20
#define PIC1_DATA       0x21
#define PIC2_COMMAND    0xA0
#define PIC2_DATA       0xA1

#define PIC_EOI         0x20
#define PIC_REG_ISR     0x0B

#define ICW1_ICW4       0x01
#define ICW1_INIT       0x10
#define ICW4_8086       0x01

static void IoWait(void) {
    asm volatile ("nop");
}

void SendPicEoi(int irq_num) {
    if (irq_num >= PIC_IRQ_BASE + 8) {
        outb(PIC2_COMMAND, PIC_EOI);
    }
    outb(PIC1_COMMAND, PIC_EOI);
}

/*
* Change which interrupt numbers are used by the IRQs. They will initially
* use interrupts 0 through 15, which isn't very good as it conflicts with the
* interrupt numbers for the CPU exceptions. 
*/
static void RemapPic(int offset) {
    uint8_t mask1 = inb(PIC1_DATA);
    uint8_t mask2 = inb(PIC2_DATA);

    outb(PIC1_COMMAND, ICW1_INIT | ICW1_ICW4);
    IoWait();
    outb(PIC2_COMMAND, ICW1_INIT | ICW1_ICW4);
    IoWait();
    outb(PIC1_DATA, offset);
    IoWait();
    outb(PIC2_DATA, offset + 8);
    IoWait();
    outb(PIC1_DATA, 4);
    IoWait();
    outb(PIC2_DATA, 2);
    IoWait();

    outb(PIC1_DATA, ICW4_8086);
    IoWait();
    outb(PIC2_DATA, ICW4_8086);
    IoWait();

    outb(PIC1_DATA, mask1);
    outb(PIC2_DATA, mask2);
}

/**
 * Set which IRQ numbers are disabled. `disabled_irqs` is a bitfield, with a set
 * bit indicating a disabled IRQ line. 0x0000 enables all, 0xFFFF disables all.
 */
void DisablePicLines(uint16_t disabled_irqs) {
    static uint16_t prev = 0xFFFF;
    if (prev != disabled_irqs) {
        outb(PIC1_DATA, disabled_irqs & 0xFF);
        outb(PIC2_DATA, disabled_irqs >> 8);
        prev = disabled_irqs;
    }
}

void x86InitPic(void) {
    RemapPic(PIC_IRQ_BASE);
    DisablePicLines(0x0000);
}
