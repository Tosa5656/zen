#pragma once

#ifndef IDT_H
#define IDT_H

#include <stdint.h>

#define IDT_TA_INTERRUPT_GATE 0x0E
#define IDT_TA_TRAP_GATE 0x0F
#define IDT_TA_DESCRIPTOR_DPL0 0x00
#define IDT_TA_DESCRIPTOR_DPL3 0x60
#define IDT_TA_PRESENT 0x80

typedef struct
{
	uint16_t offset_low;
	uint16_t selector;
	uint8_t ist;
	uint8_t type_attributes;
	uint16_t offset_mid;
	uint32_t offset_high;
	uint32_t reserved;
} __attribute__((packed)) idt_entry_t;

typedef struct
{
	uint16_t limit;
	uint64_t base;
} __attribute__((packed)) idt_ptr_t;

typedef struct
{
	uint64_t r15, r14, r13, r12, r11, r10, r9, r8;
	uint64_t rbp, rdi, rsi, rdx, rcx, rbx, rax;
	uint64_t interrupt_number;
	uint64_t error_code;
	uint64_t rip, cs, rflags, rsp, ss;
} __attribute__((packed)) cpu_registers_t;

void idt_init(void);

#endif
