#pragma once

#include <stddef.h>
#include <stdint.h>

struct tss;

void x86InitGdt(void);
void x86InitIdt(void);
void x86InitPic(void);
void x86InitPit(int hertz);
void x86InitTss(void);
uint16_t x86AddTssToGdt(struct tss* tss);

void x86InitCmos(void);
uint8_t ReadCmos(uint8_t reg);
void WriteCmos(uint8_t reg, uint8_t data);

void InitPs2(void);

void SendPicEoi(int num);

#define PIC_IRQ_BASE 32

struct x86_regs
{
	/*
	* The registers that are pushed to us in x86/asm/interrupt.s
	* 
	* SS is the first thing pushed, and thus the last to be popped
	* GS is the last thing pushed, and thus the first to be popped
	*/
	size_t gs, fs, es, ds;
	size_t edi, esi, ebp, esp, ebx, edx, ecx, eax;
	size_t int_no, err_code;
	size_t eip, cs, eflags, useresp, ss;
};

static inline void outb(uint16_t port, uint8_t val) {
    __asm__ volatile ("outb %b0, %w1" : : "a"(val), "Nd"(port) : "memory");
}

static inline uint8_t inb(uint16_t port) {
    uint8_t ret;
    __asm__ volatile ("inb %w1, %b0" : "=a"(ret) : "Nd"(port) : "memory");
    return ret;
}