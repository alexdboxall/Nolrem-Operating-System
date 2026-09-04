#pragma once

#include <stddef.h>

void* AllocHeap(size_t bytes);
void FreeHeap(void* ptr);
void* ReallocHeap(void* ptr, size_t new_size);
size_t GetAllocationSize(void* ptr);

void InitBootstrapHeap(void);

char* KeStrdup(const char* str);