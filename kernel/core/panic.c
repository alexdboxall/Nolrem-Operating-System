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
    "System message box full",
    "Bad kernel file",
    "Driver fault",
    "Not implemented"
};

static _Noreturn void KePanicEx(int panic_reason, const char* msg1, const char* msg2) {
    KeLogPrintf("\x01PANIC - ");
    KeLogPrintf("%d\n", panic_reason);
    KeLogPrintf("%s", msg1);
    if (msg2 != NULL) {
        KeLogPrintf(": %s\n", msg2);
    }
    KeDisplayPanic(msg1);
    while (true) {
        ArchDisableInterrupts();
        ArchIdle();
    }
}

export _Noreturn void KePanic(int panic_reason) {
    char* msg = "PANIC_UNKNOWN_REASON";
    if (panic_reason < (int) (sizeof(panic_msgs) / sizeof(panic_msgs[0]))) {
        msg = panic_msgs[panic_reason];
    }
    KePanicEx(panic_reason, msg, NULL);
}

export _Noreturn void KeAssertionFail(const char* file, const char* line, const char* condition, const char* msg) {
    KeLogPrintf("Assertion Failure:\n    %s\n    %s\n    %s\n    Line %s\n", msg, condition, file, line);
    KePanicEx(PANIC_ASSERTION_FAILURE, msg, condition);
}