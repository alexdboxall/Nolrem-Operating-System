#include <stddef.h>
#include <common.h>

/* We use EDX before EBX, as EBX needs to be preserved, which makes the syscalls
   longer. EDX can be trashed, so the compiler doesn't need to push/pop it.

   x86HandleInterrupt is of course in on the game:
    r->eax = KePerformSystemCall(r->eax, r->edx, r->ecx, r->ebx, r->esi);
 */

export size_t SystemCall(size_t arg1, size_t arg2, size_t arg3, size_t arg4, size_t arg5) {
    size_t result;

    __asm__ __volatile__ (
        "int $0x60" // 96 in decimal is 0x60 in hex
        : "=a" (result)  // Output: eax stores the return value
        : "a" (arg1),    // Input: eax
          "d" (arg2),    // Input: edx
          "c" (arg3),    // Input: ecx
          "b" (arg4),    // Input: ebx
          "S" (arg5)     // Input: esi
        : "memory"       // Clobber: tells compiler memory might change
    );

    return result;
}

export size_t SystemCall1(size_t arg1) {
    size_t result;

    __asm__ __volatile__ (
        "int $0x60" // 96 in decimal is 0x60 in hex
        : "=a" (result)  // Output: eax stores the return value
        : "a" (arg1)     // Input: eax
        : "memory"       // Clobber: tells compiler memory might change
    );

    return result;
}

export size_t SystemCall2(size_t arg1, size_t arg2) {
    size_t result;

    __asm__ __volatile__ (
        "int $0x60" // 96 in decimal is 0x60 in hex
        : "=a" (result)  // Output: eax stores the return value
        : "a" (arg1),    // Input: eax
          "d" (arg2)     // Input: edx
        : "memory"       // Clobber: tells compiler memory might change
    );

    return result;
}

export size_t SystemCall3(size_t arg1, size_t arg2, size_t arg3) {
    size_t result;

    __asm__ __volatile__ (
        "int $0x60" // 96 in decimal is 0x60 in hex
        : "=a" (result)  // Output: eax stores the return value
        : "a" (arg1),    // Input: eax
          "d" (arg2),    // Input: edx
          "c" (arg3)     // Input: ecx
        : "memory"       // Clobber: tells compiler memory might change
    );

    return result;
}

export size_t SystemCall4(size_t arg1, size_t arg2, size_t arg3, size_t arg4) {
    size_t result;

    __asm__ __volatile__ (
        "int $0x60" // 96 in decimal is 0x60 in hex
        : "=a" (result)  // Output: eax stores the return value
        : "a" (arg1),    // Input: eax
          "d" (arg2),    // Input: edx
          "c" (arg3),    // Input: ecx
          "b" (arg4)     // Input: ebx
        : "memory"       // Clobber: tells compiler memory might change
    );

    return result;
}
