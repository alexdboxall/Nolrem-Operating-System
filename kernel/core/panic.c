#include <common.h>
#include <log.h>
#include <kgfx.h>

char* panic_msgs[] = {
    "Cannot allocate without faulting",
    "Out of memory",
    "Heap request too large"
};

_Noreturn void Panic(int panic_reason) {
    KernelDisplayPanic(panic_msgs[panic_reason]);
    while (true) {
        ;
    }
}