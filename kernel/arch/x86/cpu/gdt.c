
#include <common.h>
#include <log.h>
#include <arch.h>
#include <cpu.h>

struct gdt_entry
{
	uint16_t limit_low;
	uint16_t base_low;
	uint8_t base_middle;
	uint8_t access;
	uint8_t flags_and_limit_high;
	uint8_t base_high;
	
} __attribute__((packed));

struct gdt_ptr
{
	uint16_t size;
	size_t location;
} __attribute__((packed));


static struct gdt_entry CreateGdtEntry(size_t base, size_t limit, uint8_t access, uint8_t gran)
{
	return (struct gdt_entry) {
		.base_low 			  = base & 0xFFFF, 
		.base_middle 	  	  = (base >> 16) & 0xFF, 
		.base_high 			  = (base >> 24) & 0xFF, 
		.limit_low 			  = limit & 0xFFFF,
		.flags_and_limit_high = ((limit >> 16) & 0xF) | ((gran & 0xF) << 4),
		.access 			  = access,
	};
}

static struct gdt_entry gdt[6];
static struct gdt_ptr gdtr;

void x86LoadGdt(size_t gdtPtr) {
    __asm__ volatile (
        "lgdt (%0)\n\t"
        "ljmp $0x08, $1f\n\t"
        "1:\n\t"
        "mov $0x10, %%ax\n\t"
        "mov %%ax, %%ds\n\t"
        "mov %%ax, %%es\n\t"
        "mov %%ax, %%ss\n\t"
        :
        : "r" (gdtPtr)
        : "eax", "memory"
    );
}

void x86InitGdt(void) {
	gdt[0] = CreateGdtEntry(0, 0, 0, 0);			   // null segment
	gdt[1] = CreateGdtEntry(0, 0xFFFFFFFF, 0x9A, 0xC); // kernel code
	gdt[2] = CreateGdtEntry(0, 0xFFFFFFFF, 0x92, 0xC); // kernel data
	gdt[3] = CreateGdtEntry(0, 0xFFFFFFFF, 0xFA, 0xC); // user code
	gdt[4] = CreateGdtEntry(0, 0xFFFFFFFF, 0xF2, 0xC); // user data

	gdtr.size = sizeof(gdt) - 1;
	gdtr.location = (size_t) &gdt;

	x86LoadGdt((size_t) &gdtr);
}

uint16_t x86AddTssToGdt(struct tss* tss) {
	gdt[5] = CreateGdtEntry((size_t) tss, sizeof(struct tss), 0x89, 0x0);
	return 5 * 0x8;
}