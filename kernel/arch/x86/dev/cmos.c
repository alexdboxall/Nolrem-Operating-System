#include <machine/x86.h>
#include <arch.h>
#include <common.h>
#include <spinlock.h>

static struct spinlock cmos_spinlock;
static bool nmi_on = true;

#define CMOS_ADDR 0x70
#define CMOS_DATA 0x71

static void x86CmosDelay(void) {
    x86IoDelay();
}

static void x86SelectAddress(uint8_t addr) {
    outb(CMOS_ADDR, (addr & 0x7F) | (nmi_on ? 0 : 0x80));
    x86CmosDelay();
}

uint8_t x86ReadCmos(uint8_t reg) {
    KeAcquireSpinlock(&cmos_spinlock);
    x86SelectAddress(reg);
    uint8_t retv = inb(CMOS_DATA);
    x86CmosDelay();
    KeReleaseSpinlock(&cmos_spinlock);
    return retv;
} 

void x86WriteCmos(uint8_t reg, uint8_t data) {
    KeAcquireSpinlock(&cmos_spinlock);
    x86SelectAddress(reg);
    outb(CMOS_DATA, data);
    x86CmosDelay();
    KeReleaseSpinlock(&cmos_spinlock);
} 

void x86SetNmiEnable(bool enable) {
    nmi_on = enable;

    /*
     * Read anything from the CMOS - address selection takes in the NMI flag.
     */
    (void) x86ReadCmos(0x10);
}

void x86InitCmos(void) {
    KeInitSpinlock(&cmos_spinlock);
    x86SetNmiEnable(true);
}
