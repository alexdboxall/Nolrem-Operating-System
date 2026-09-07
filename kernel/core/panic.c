#include <common.h>
#include <log.h>
#include <kgfx.h>

char* panic_msgs[] = {
    "Cannot allocate without faulting",
    "Out of memory",
    "Heap request too large",
    "Page fault in non-paged area",
    "Livelock detected in VMM",
    "Out of memory",
    "Invalid arch operation",
    "Idle thread has blocked",
};

_Noreturn void Panic(int panic_reason) {
    LogPrintf("PANIC: %d\n", panic_reason);
    LogPrintf("%s\n", panic_msgs[panic_reason]);
    KernelDisplayPanic(panic_msgs[panic_reason]);
    while (true) {
        ;
    }
}