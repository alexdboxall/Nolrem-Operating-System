#pragma once

enum panic_codes {
        PANIC_CANNOT_MALLOC_WITHOUT_FAULTING,
        PANIC_OUT_OF_HEAP,
        PANIC_HEAP_REQUEST_TOO_LARGE,
};

_Noreturn void Panic(int panic_reason);

