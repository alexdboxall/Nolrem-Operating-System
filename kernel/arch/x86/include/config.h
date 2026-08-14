#pragma once

#define PAGE_SIZE 4096

struct x86_regs;
typedef struct x86_regs* irqcontext_t;

#define ARCH_USER_AREA_BASE         0x08000000
#define ARCH_USER_AREA_LIMIT        0xC0000000

#define ARCH_KRNL_VIRT_RANGE_BASE   0xC8000000
#define ARCH_KRNL_VIRT_RANGE_BYTES  0x30000000

