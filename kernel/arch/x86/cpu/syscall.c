#include <stddef.h>

size_t SystemCall(size_t arg1, size_t arg2, size_t arg3, size_t arg4, size_t arg5) {
    size_t result;

    __asm__ __volatile__ (
        "int $0x60" // 96 in decimal is 0x60 in hex
        : "=a" (result)  // Output: eax stores the return value
        : "a" (arg1),    // Input: eax
          "b" (arg2),    // Input: ebx
          "c" (arg3),    // Input: ecx
          "d" (arg4),    // Input: edx
          "S" (arg5)     // Input: esi
        : "memory"       // Clobber: tells compiler memory might change
    );

    return result;
}