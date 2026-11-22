
#include <common.h>
#include <log.h>
#include <arch.h>
#include <cpu.h>
#include <heap.h>
#include <panic.h>

struct idt_entry
{
	uint16_t isr_offset_low;
	uint16_t segment_selector;
	uint8_t reserved;
	uint8_t type;
	uint16_t isr_offset_high;

} __attribute__((packed));

struct idt_ptr
{
	uint16_t size;
	size_t location;
} __attribute__((packed));

extern size_t isr_vectors_first_33;

/*
 * One of these is allocated for each CPU, and hangs off of the CPU's
 * platform_cpu_data_t. Only this file knows the layout (the pointer in
 * platform_cpu_data_t is to an opaque `struct idt`).
 */
struct idt
{
	struct idt_entry entries[256];
	struct idt_ptr ptr;
};

static void x86SetIdtEntry(struct idt* idt, int num, size_t isr_addr, uint8_t type)
{
	idt->entries[num].isr_offset_low = (isr_addr & 0xFFFF);
	idt->entries[num].isr_offset_high = (isr_addr >> 16) & 0xFFFF;
	idt->entries[num].segment_selector = 0x08;
	idt->entries[num].reserved = 0;
	idt->entries[num].type = type;
}

void x86LoadIdt(size_t idtPtr) {
    __asm__ volatile (
        "lidt (%0)"
        :
        : "r" (idtPtr)
        : "memory"
    );
}

void x86InitIdt(void) {
	platform_cpu_data_t* cpu_data = KeGetCpu()->platform_specific;

	cpu_data->idt = KeAllocPermanentHeap(sizeof(struct idt));
	if (cpu_data->idt == NULL) {
		KePanic(PANIC_OUT_OF_MEMORY);
	}
	struct idt* idt = cpu_data->idt;

	size_t* isr_vectors = &isr_vectors_first_33;

	for (int i = 0; i < 32; ++i) {
		x86SetIdtEntry(idt, i, isr_vectors[i], 0x8E);
	}
	for (int i = 32; i < 256; ++i) {
		size_t vector = ((size_t)isr_vectors[32]) + 4 * (i - 32) + 6 * ((i - 16) / 32);
		x86SetIdtEntry(idt, i, vector, i == 96 ? 0xEE : 0x8E);
	}

	idt->ptr.location = (size_t) &idt->entries;
	idt->ptr.size = sizeof(idt->entries) - 1;
	
	x86LoadIdt((size_t) &idt->ptr);
}
