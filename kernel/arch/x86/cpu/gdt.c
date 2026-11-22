
#include <common.h>
#include <log.h>
#include <arch.h>
#include <cpu.h>
#include <heap.h>
#include <machine/x86.h>

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


static struct gdt_entry x86CreateGdtEntry(size_t base, size_t limit, uint8_t access, uint8_t gran)
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

/*
 * One of these is allocated for each CPU, and hangs off of the CPU's
 * platform_cpu_data_t. Only this file knows the layout (the pointer in
 * platform_cpu_data_t is to an opaque `struct gdt`).
 */
struct gdt
{
	struct gdt_entry entries[6];
	struct gdt_ptr ptr;
};

static struct gdt* x86GetGdt(void) {
	return KeGetCpu()->platform_specific->gdt;
}

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
	platform_cpu_data_t* cpu_data = KeGetCpu()->platform_specific;

	cpu_data->gdt = KeAllocPermanentHeap(sizeof(struct gdt));
	struct gdt* gdt = cpu_data->gdt;

	gdt->entries[0] = x86CreateGdtEntry(0, 0, 0, 0);			   // null segment
	gdt->entries[1] = x86CreateGdtEntry(0, 0xFFFFFFFF, 0x9A, 0xC); // kernel code
	gdt->entries[2] = x86CreateGdtEntry(0, 0xFFFFFFFF, 0x92, 0xC); // kernel data
	gdt->entries[3] = x86CreateGdtEntry(0, 0xFFFFFFFF, 0xFA, 0xC); // user code
	gdt->entries[4] = x86CreateGdtEntry(0, 0xFFFFFFFF, 0xF2, 0xC); // user data
	gdt->entries[5] = x86CreateGdtEntry(0, 0, 0, 0);			   // TSS (filled in by x86AddTssToGdt)

	gdt->ptr.size = sizeof(gdt->entries) - 1;
	gdt->ptr.location = (size_t) &gdt->entries;

	x86LoadGdt((size_t) &gdt->ptr);
}

uint16_t x86AddTssToGdt(struct tss* tss) {
	struct gdt* gdt = x86GetGdt();
	gdt->entries[5] = x86CreateGdtEntry((size_t) tss, sizeof(struct tss) - 1, 0x89, 0x0);
	return 5 * 0x8;
}
