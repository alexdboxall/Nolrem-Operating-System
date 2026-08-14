#pragma once

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <stdarg.h>
#include <stdatomic.h>

#ifdef COMPILE_KERNEL
#include <sys/types.h>
#endif

#define OS_VERSION_STRING   "TinyOS"
#define OS_VERSION_MAJOR    0x00
#define OS_VERSION_MINOR    0x01

#ifndef NULL
#define NULL ((void*) 0)
#endif

#define warn_unused __attribute__((warn_unused_result))
#define always_inline __attribute__((always_inline)) inline

#define export __attribute__((used)) __attribute__((visibility ("default")))

#define userexec __attribute__((section(".kuser")))
#define userrodata __attribute__((section(".kuserrodata")))

#define pageable __attribute__((section(".pageable")))
#define pageablerodata __attribute__((section(".pageablerodata")))

#define MAX(a, b) ((a) > (b) ? (a) : (b))
#define MIN(a, b) ((a) < (b) ? (a) : (b))
#define CLAMP(val, min, max) MAX(MIN(val, max), min)
#define COMPARE_SIGN(a, b) ((a) > (b) ? 1 : ((a) < (b) ? -1 : 0))

#define AddVoidPtr(p, o) ((void*)(((uint8_t*)(p))+(o)))
#define SubVoidPtr(p, o) ((void*)(((uint8_t*)(p))-(o)))


#ifdef COMPILE_KERNEL

void* KeAllocHeap(size_t len);
size_t KeGetAllocationSize(void* ptr);
void KeFreeHeap(void* ptr);
void* KeReallocHeap(void* ptr, size_t new_size);

#define AllocHeap(len) KeAllocHeap(len)
#define GetAllocationSize(ptr) KeGetAllocationSize(ptr)
#define FreeHeap(ptr) KeFreeHeap(ptr)
#define ReallocHeap(ptr, new_size) KeReallocHeap(ptr, new_size)

#endif