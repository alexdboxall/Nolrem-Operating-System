
#include <common.h>
#include <log.h>

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

static struct idt_entry idt[256];
static struct idt_ptr idtr;

static void x86SetIdtEntry(int num, size_t isr_addr, uint8_t type)
{
	idt[num].isr_offset_low = (isr_addr & 0xFFFF);
	idt[num].isr_offset_high = (isr_addr >> 16) & 0xFFFF;
	idt[num].segment_selector = 0x08;
	idt[num].reserved = 0;
	idt[num].type = type;
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
	size_t* isr_vectors = &isr_vectors_first_33;

	for (int i = 0; i < 32; ++i) {
		x86SetIdtEntry(i, isr_vectors[i], 0x8E);
	}
	for (int i = 32; i < 256; ++i) {
		size_t vector = ((size_t)isr_vectors[32]) + 4 * (i - 32) + 6 * ((i - 16) / 32);
		x86SetIdtEntry(i, vector, i == 96 ? 0xEE : 0x8E);
	}

	idtr.location = (size_t) &idt;
	idtr.size = sizeof(idt) - 1;
	
	x86LoadIdt((size_t) &idtr);
}