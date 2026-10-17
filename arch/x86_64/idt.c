#include "idt.h"
#include "serial.h"
#include "pic.h"
#include "apic.h"
#include "timer.h"
#include "io.h"

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

void exception_handler(cpu_registers_t* registers)
{
	serial_write("\r\n!!! KERNEL PANIC !!!\r\n");
	if (registers->interrupt_number < 32)
	{
		serial_write("Exception: ");
		serial_write(exception_messages[registers->interrupt_number]);
		serial_write("\r\n");
	}

	serial_field("Interrupt vector", registers->interrupt_number);
	serial_field("Error code", registers->error_code);
	serial_field("Instruction RIP" , registers->rip);
	serial_field("Code segment CS ", registers->cs);
	serial_field("Stack pointer RSP", registers->rsp);

	if (registers->interrupt_number == 14)
	{
		uint64_t cr2_val;
		__asm__ volatile("mov %%cr2, %0" : "=r"(cr2_val));
		serial_field("Faulty address CR2", cr2_val);
	}

	while(1)
	{
		__asm__ volatile("cli; hlt");
	}
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

	load_idt((uint64_t)&idt_pointer);

	__asm__ volatile("sti");

	serial_write("IDT loaded and interrupts enabled!\r\n");
}

void irq_handler(cpu_registers_t* registers)
{
	uint64_t irq_number = registers->interrupt_number - 32;

	if (irq_number == 0)
	{
		timer_handler();
	}
	else if (irq_number == 1)
	{
		uint8_t scancode = inb(0x60);
		serial_write("Key pressed! Scancode: ");
		serial_dec(scancode);
		serial_write("\r\n");
	}
	else
	{
		serial_write("Unhandled IRQ: ");
		serial_dec(irq_number);
		serial_write("\r\n");
	}

	if (apic_is_enabled())
	{
		apic_send_eoi();
	}
	else
	{
		pic_send_eoi(irq_number);
	}
}
