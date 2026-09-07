#pragma once

#define PAGE_SIZE 4096

struct x86_regs;
typedef struct x86_regs* irqcontext_t;

#define ARCH_USER_AREA_BASE         0x08000000
#define ARCH_USER_AREA_LIMIT        0xC0000000

#define ARCH_KRNL_MAPPING_BASE      0xC0000000

#define ARCH_KRNL_VIRT_RANGE_BASE   0xC8000000
#define ARCH_KRNL_VIRT_RANGE_BYTES  0x30000000

#define ARCH_MAX_CPUS               16

#include <stdint.h>

struct tss {
	uint16_t link;			// used
	uint16_t unused_1;
	uint32_t esp0;			// used
	uint16_t ss0;			// used
	uint8_t unused_2[92];
	uint16_t iopb;			// used
	
} __attribute__((packed));

typedef struct {
    /* Need to keep TSS at the top, thread switching assembly needs it. */
    struct tss* tss;

    struct gdt* gdt;
    struct idt* idt;

} platform_cpu_data_t;
