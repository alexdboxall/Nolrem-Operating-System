#include <machine/x86.h>
#include <arch.h>
#include <common.h>
#include <spinlock.h>

static struct spinlock cmos_spinlock;
static bool nmi_on = true;

#define CMOS_ADDR 0x70
#define CMOS_DATA 0x71

static void CmosDelay(void) {
    asm volatile ("nop");
}

static void SelectAddress(uint8_t addr) {
    outb(CMOS_ADDR, (addr & 0x7F) | (nmi_on ? 0 : 0x80));
    CmosDelay();
}

uint8_t ReadCmos(uint8_t reg) {
    AcquireSpinlock(&cmos_spinlock);
    SelectAddress(reg);
    uint8_t retv = inb(CMOS_DATA);
    CmosDelay();
    ReleaseSpinlock(&cmos_spinlock);
    return retv;
} 

void WriteCmos(uint8_t reg, uint8_t data) {
    AcquireSpinlock(&cmos_spinlock);
    SelectAddress(reg);
    outb(CMOS_DATA, data);
    CmosDelay();
    ReleaseSpinlock(&cmos_spinlock);
} 

void SetNmiEnable(bool enable) {
    nmi_on = enable;

    /*
     * Read anything from the CMOS - address selection takes in the NMI flag.
     */
    (void) ReadCmos(0x10);
}

void x86InitCmos(void) {
    InitSpinlock(&cmos_spinlock);
    SetNmiEnable(true);
}
