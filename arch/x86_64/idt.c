#include "idt.h"
#include "serial.h"
#include "pic.h"
#include "apic.h"
#include "timer.h"
#include "io.h"
#include "gdt.h"
#include "keyboard.h"
#include "scheduler.h"
#include "ata.h"
#include "fd.h"
#include "panic.h"

__attribute__((aligned(4096))) idt_entry_t idt_table[256] = {0};
idt_ptr_t idt_pointer;

extern void load_idt(uint64_t pointer);

extern void isr0();
extern void isr1();
extern void isr2();
extern void isr3();
extern void isr4();
extern void isr5();
extern void isr6();
extern void isr7();
extern void isr8();
extern void isr9();
extern void isr10();
extern void isr11();
extern void isr12();
extern void isr13();
extern void isr14();
extern void isr15();
extern void isr16();
extern void isr17();
extern void isr18();
extern void isr19();
extern void isr20();
extern void isr21();
extern void isr22();
extern void isr23();
extern void isr24();
extern void isr25();
extern void isr26();
extern void isr27();
extern void isr28();
extern void isr29();
extern void isr30();
extern void isr31();

extern void irq0();
extern void irq1();
extern void irq2();
extern void irq3();
extern void irq4();
extern void irq5();
extern void irq6();
extern void irq7();
extern void irq8();
extern void irq9();
extern void irq10();
extern void irq11();
extern void irq12();
extern void irq13();
extern void irq14();
extern void irq15();

extern void isr_ignored();

static const char* exception_messages[] =
{
    "Division By Zero", "Debug", "Non-Maskable Interrupt", "Breakpoint",
    "Into Detected Overflow", "Out of Bounds", "Invalid Opcode", "No Coprocessor",
    "Double Fault", "Coprocessor Segment Overrun", "Bad TSS", "Segment Not Present",
    "Stack Fault", "General Protection Fault (#GP)", "Page Fault (#PF)", "Unknown Interrupt",
    "x87 Float Exception", "Alignment Check", "Machine Check", "SIMD Float Exception",
    "Virtualization Exception", "Control Protection", "Reserved", "Reserved",
    "Reserved", "Reserved", "Reserved", "Reserved", "Reserved", "Reserved",
    "Reserved", "Reserved"
};

void idt_set_gate(uint8_t vector, void (*handler)(), uint8_t type_attributes)
{
	uint64_t address = (uint64_t)handler;

	idt_table[vector].offset_low = (uint16_t)(address & 0xFFFF);
	idt_table[vector].selector = 0x08;
	idt_table[vector].ist = 0;
	idt_table[vector].type_attributes = type_attributes;
	idt_table[vector].offset_mid = (uint16_t)((address >> 16) & 0xFFFF);
	idt_table[vector].offset_high = (uint32_t)((address >> 32) & 0xFFFFFFFF);
	idt_table[vector].reserved = 0;
}

static void dump_page_fault_error(uint64_t error)
{
	serial_write("  cause: ");
	serial_write((error & 0x1) ? "protection violation" : "page not present");
	serial_write(", ");
	serial_write((error & 0x2) ? "write" : "read");
	serial_write(", ");
	serial_write((error & 0x4) ? "user mode" : "kernel mode");

	if (error & 0x8)
	{
		serial_write(", reserved bit set");
	}

	if (error & 0x10)
	{
		serial_write(", instruction fetch");
	}

	serial_write("\r\n");
}

static void dump_general_registers(cpu_registers_t* registers)
{
	serial_write("--- registers ---\r\n");

	serial_field("RAX", registers->rax);
	serial_field("RBX", registers->rbx);
	serial_field("RCX", registers->rcx);
	serial_field("RDX", registers->rdx);
	serial_field("RSI", registers->rsi);
	serial_field("RDI", registers->rdi);
	serial_field("RBP", registers->rbp);
	serial_field("RSP", registers->rsp);
	serial_field("R8 ", registers->r8);
	serial_field("R9 ", registers->r9);
	serial_field("R10", registers->r10);
	serial_field("R11", registers->r11);
	serial_field("R12", registers->r12);
	serial_field("R13", registers->r13);
	serial_field("R14", registers->r14);
	serial_field("R15", registers->r15);
}

void exception_handler(cpu_registers_t* registers)
{
	__asm__ volatile("cli");

	serial_write("\r\n!!! KERNEL PANIC !!!\r\n");

	if (registers->interrupt_number < 32)
	{
		serial_write("Exception: ");
		serial_write(exception_messages[registers->interrupt_number]);
		serial_write("\r\n");
	}

	serial_field("Interrupt vector", registers->interrupt_number);
	serial_field("Error code", registers->error_code);

	if (registers->interrupt_number == 14)
	{
		dump_page_fault_error(registers->error_code);
	}

	serial_write("--- fault frame ---\r\n");
	serial_field("Instruction RIP", registers->rip);
	serial_field("Code segment CS ", registers->cs);
	serial_field("RFLAGS", registers->rflags);
	serial_field("Stack pointer RSP", registers->rsp);
	serial_field("Stack segment SS", registers->ss);

	dump_general_registers(registers);

	if (registers->interrupt_number == 14)
	{
		uint64_t cr2_value;
		uint64_t cr3_value;

		__asm__ volatile("mov %%cr2, %0" : "=r"(cr2_value));
		__asm__ volatile("mov %%cr3, %0" : "=r"(cr3_value));

		serial_write("--- faulting address ---\r\n");
		serial_field("CR2", cr2_value);
		serial_field("CR3", cr3_value);
	}

	kernel_panic_halt();
}

void idt_init(void)
{
	idt_pointer.limit = (sizeof(idt_entry_t) * 256) - 1;
	idt_pointer.base  = (uint64_t)&idt_table;

	uint8_t flags = IDT_TA_PRESENT | IDT_TA_INTERRUPT_GATE | IDT_TA_DESCRIPTOR_DPL0;

	void (*isr_stubs[32])(void) =
	{
		isr0, isr1, isr2, isr3, isr4, isr5, isr6, isr7,
		isr8, isr9, isr10, isr11, isr12, isr13, isr14, isr15,
		isr16, isr17, isr18, isr19, isr20, isr21, isr22, isr23,
		isr24, isr25, isr26, isr27, isr28, isr29, isr30, isr31
	};

	for (uint8_t i = 0; i < 32; i++)
	{
		idt_set_gate(i, isr_stubs[i], flags);
	}

	idt_table[8].ist = IST_DOUBLE_FAULT;

	pic_remap();

	timer_init(100);

	uint8_t irq_flags = IDT_TA_PRESENT | IDT_TA_DESCRIPTOR_DPL0 | IDT_TA_INTERRUPT_GATE;

	void (*irq_stubs[16])(void) =
	{
		irq0, irq1, irq2, irq3, irq4, irq5, irq6, irq7, irq8,
		irq9, irq10, irq11, irq12, irq13, irq14, irq15
	};
	for (uint8_t i = 0; i < 16; i++)
	{
		idt_set_gate(i + 32, irq_stubs[i], irq_flags);
	}

	for (uint16_t i = 48; i < 256; i++)
	{
		idt_set_gate((uint8_t)i, isr_ignored, irq_flags);
	}

	load_idt((uint64_t)&idt_pointer);

	serial_write("IDT loaded!\r\n");
}

void irq_handler(cpu_registers_t* registers)
{
	uint64_t irq_number = registers->interrupt_number - 32;

	if (apic_is_enabled())
	{
		apic_send_eoi();
	}
	else
	{
		pic_send_eoi(irq_number);
	}

	if (irq_number == 0)
	{
		timer_handler();
		scheduler_check_sleeping();
	}
	else if (irq_number == 1)
	{
		keyboard_push(inb(0x60));
		fd_wait_wake_all();
	}
	else if (irq_number == 14 || irq_number == 15)
	{
		ata_irq_handler();
	}
	else
	{
		serial_write("Unhandled IRQ: ");
		serial_dec(irq_number);
		serial_write("\r\n");
	}
}
