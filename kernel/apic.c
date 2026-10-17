#include "apic.h"
#include "acpi.h"
#include "memory.h"
#include "pic.h"
#include "serial.h"

#define IA32_APIC_BASE_MSR 0x1B
#define APIC_BASE_ENABLE (1ULL << 11)

#define LAPIC_ID 0x20
#define LAPIC_TPR 0x80
#define LAPIC_EOI 0xB0
#define LAPIC_SVR 0xF0
#define LAPIC_LVT_TIMER 0x320
#define LAPIC_LVT_LINT0 0x350
#define LAPIC_LVT_LINT1 0x360
#define LAPIC_LVT_ERROR 0x370

#define LAPIC_SPURIOUS_VECTOR 0xFF
#define LAPIC_LVT_MASKED (1U << 16)

#define IOAPIC_REGSEL 0x00
#define IOAPIC_IOWIN 0x10
#define IOAPIC_VERSION 0x01
#define IOAPIC_REDIRECTION_TABLE 0x10

#define VECTOR_TIMER 32
#define VECTOR_KEYBOARD 33

static volatile uint32_t *lapic = NULL;
static volatile uint32_t *ioapic = NULL;
static bool apic_ready = false;

static uint64_t read_msr(uint32_t msr)
{
	uint32_t low;
	uint32_t high;

	__asm__ volatile("rdmsr" : "=a"(low), "=d"(high) : "c"(msr));

	return ((uint64_t)high << 32) | low;
}

static void write_msr(uint32_t msr, uint64_t value)
{
	__asm__ volatile("wrmsr" :: "c"(msr), "a"((uint32_t)value), "d"((uint32_t)(value >> 32)));
}

static void lapic_write(uint32_t reg, uint32_t value)
{
	lapic[reg / 4] = value;
}

static uint32_t lapic_read(uint32_t reg)
{
	return lapic[reg / 4];
}

static uint32_t ioapic_read(uint32_t reg)
{
	ioapic[IOAPIC_REGSEL / 4] = reg;
	return ioapic[IOAPIC_IOWIN / 4];
}

static void ioapic_write(uint32_t reg, uint32_t value)
{
	ioapic[IOAPIC_REGSEL / 4] = reg;
	ioapic[IOAPIC_IOWIN / 4] = value;
}

static void map_mmio(uint64_t physical)
{
	uint64_t before = boot_alloc_current;

	virtual_memory_manager_map_page(physical, physical,
		PAGE_WRITABLE | PAGE_WRITE_THROUGH | PAGE_CACHE_DISABLE);

	if (boot_alloc_current > before)
	{
		physical_memory_manager_lock_region(before, boot_alloc_current - before);
	}
}

static void ioapic_route(uint32_t global_system_interrupt_base, uint32_t gsi, uint8_t vector, uint16_t iso_flags)
{
	if (gsi < global_system_interrupt_base)
	{
		return;
	}

	uint32_t index = gsi - global_system_interrupt_base;
	uint32_t destination = (lapic_read(LAPIC_ID) >> 24) & 0xFF;

	uint32_t low = vector;

	if ((iso_flags & 0x3) == 0x3)
	{
		low |= (1U << 13);
	}
	if (((iso_flags >> 2) & 0x3) == 0x3)
	{
		low |= (1U << 15);
	}

	uint32_t high = destination << 24;

	ioapic_write(IOAPIC_REDIRECTION_TABLE + 2 * index, low);
	ioapic_write(IOAPIC_REDIRECTION_TABLE + 2 * index + 1, high);
}

static acpi_interrupt_source_t *irq_to_source(acpi_madt_info_t *madt, uint8_t irq)
{
	for (uint32_t i = 0; i < madt->interrupt_source_count; i++)
	{
		if (madt->interrupt_sources[i].irq_source == irq)
		{
			return &madt->interrupt_sources[i];
		}
	}

	return NULL;
}

void apic_init(void)
{
	__asm__ volatile("cli");

	serial_write("APIC: initializing...\r\n");

	acpi_madt_info_t *madt = acpi_get_madt_info();

	uint64_t lapic_physical = madt->local_apic_address;
	if (lapic_physical == 0)
	{
		lapic_physical = 0xFEE00000;
	}

	map_mmio(lapic_physical);

	serial_field("APIC: base MSR before ", read_msr(IA32_APIC_BASE_MSR));

	uint64_t apic_base = (lapic_physical & 0xFFFFFFFFFFFFF000ULL) | APIC_BASE_ENABLE;
	write_msr(IA32_APIC_BASE_MSR, apic_base);

	lapic = (volatile uint32_t *)lapic_physical;

	lapic_write(LAPIC_TPR, 0);
	lapic_write(LAPIC_SVR, 0x100 | LAPIC_SPURIOUS_VECTOR);
	lapic_write(LAPIC_LVT_TIMER, LAPIC_LVT_MASKED);
	lapic_write(LAPIC_LVT_LINT0, LAPIC_LVT_MASKED);
	lapic_write(LAPIC_LVT_LINT1, LAPIC_LVT_MASKED);
	lapic_write(LAPIC_LVT_ERROR, LAPIC_LVT_MASKED);

	serial_field("APIC: local APIC id ", lapic_read(LAPIC_ID) >> 24);

	uint32_t gsi_base = 0;

	if (madt->ioapic_count > 0)
	{
		uint64_t ioapic_physical = madt->ioapics[0].address;
		map_mmio(ioapic_physical);

		ioapic = (volatile uint32_t *)ioapic_physical;
		gsi_base = madt->ioapics[0].global_system_interrupt_base;

		uint32_t max_redirection_entry = (ioapic_read(IOAPIC_VERSION) >> 16) & 0xFF;

		for (uint32_t i = 0; i <= max_redirection_entry; i++)
		{
			ioapic_write(IOAPIC_REDIRECTION_TABLE + 2 * i, LAPIC_LVT_MASKED);
		}

		acpi_interrupt_source_t *timer_source = irq_to_source(madt, 0);
		acpi_interrupt_source_t *keyboard_source = irq_to_source(madt, 1);

		uint32_t timer_gsi = (timer_source != NULL) ? timer_source->global_system_interrupt : 0;
		uint32_t keyboard_gsi = (keyboard_source != NULL) ? keyboard_source->global_system_interrupt : 1;

		ioapic_route(gsi_base, timer_gsi, VECTOR_TIMER, (timer_source != NULL) ? timer_source->flags : 0);
		ioapic_route(gsi_base, keyboard_gsi, VECTOR_KEYBOARD, (keyboard_source != NULL) ? keyboard_source->flags : 0);
	}
	else
	{
		serial_write("APIC: no IOAPIC in MADT, interrupts stay on PIC!\r\n");
		apic_ready = false;
		__asm__ volatile("sti");
		return;
	}

	pic_disable();

	apic_ready = true;
	serial_write("APIC: initialized\r\n");

	__asm__ volatile("sti");
}

void apic_send_eoi(void)
{
	if (lapic != NULL)
	{
		lapic_write(LAPIC_EOI, 0);
	}
}

bool apic_is_enabled(void)
{
	return apic_ready;
}
