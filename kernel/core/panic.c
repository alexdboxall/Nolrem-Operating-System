#include <common.h>
#include <log.h>
#include <arch.h>
#include <kgfx.h>
#include <panic.h>

char* panic_msgs[] = {
    "Cannot allocate without faulting",
    "Out of memory",
    "Heap request too large",
    "Page fault in non-paged area",
    "Livelock detected in VMM",
    "Out of memory",
    "Invalid arch operation",
    "Idle thread has blocked",
    "Fuck me",
    "Assertion failure bootstrapping VMM special heap",
    "Assertion failure",
    "System message box full"
};

static _Noreturn void PanicEx(int panic_reason, const char* msg1, const char* msg2) {
    LogPrintf("PANIC - ");
    LogPrintf("%d\n", panic_reason);
    LogPrintf("%s", msg1);
    if (msg2 != NULL) {
        LogPrintf(": %s\n", msg2);
    }
    KernelDisplayPanic(msg1);
    while (true) {
        ArchDisableInterrupts();
        ArchIdle();
    }
}

export _Noreturn void Panic(int panic_reason) {
    char* msg = "PANIC_UNKNOWN_REASON";
    if (panic_reason < (int) (sizeof(panic_msgs) / sizeof(panic_msgs[0]))) {
        msg = panic_msgs[panic_reason];
    }
    PanicEx(panic_reason, msg, NULL);
}

export _Noreturn void AssertionFail(const char* file, const char* line, const char* condition, const char* msg) {
    LogPrintf("Assertion Failure:\n    %s\n    %s\n    %s\n    Line %s\n", msg, condition, file, line);
    PanicEx(PANIC_ASSERTION_FAILURE, msg, condition);
}