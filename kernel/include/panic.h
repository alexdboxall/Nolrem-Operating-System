#pragma once

enum panic_codes {
        PANIC_CANNOT_MALLOC_WITHOUT_FAULTING,
        PANIC_OUT_OF_HEAP,
        PANIC_HEAP_REQUEST_TOO_LARGE,
        PANIC_PAGE_FAULT_IN_NON_PAGED_AREA,
        PANIC_VMM_LIVELOCK,
        PANIC_OUT_OF_MEMORY,
        PANIC_INVALID_ARCH_OPERATION,
        PANIC_IDLE_TASK_HAS_BLOCKED,
        PANIC_FUCK_ME
};

_Noreturn void Panic(int panic_reason);

