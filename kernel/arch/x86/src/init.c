#include <machine/x86.h>
#include <log.h>
#include <timeconv.h>
#include <arch.h>

void ArchInit(void) {
    x86InitGdt();
    x86InitIdt();
    x86InitPic();
    x86InitPit(25);
    asm volatile ("sti");

    x86InitCmos();
    InitPs2();
}
