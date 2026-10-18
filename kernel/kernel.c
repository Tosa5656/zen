#include "boot_info.h"
#include "memory.h"
#include "serial.h"
#include "timer.h"
#include "acpi.h"
#include "apic.h"
#include "gdt.h"
#include "heap.h"
#include "keyboard.h"
#include <idt.h>

extern uint8_t _kernel_start[];
extern uint8_t _kernel_end[];

static void dump_boot_info(const boot_info_t *info)
{
	if (info == 0)
	{
		serial_write("boot_info pointer is NULL!\r\n");
		return;
	}

	serial_field("magic                       ", info->magic);
	serial_field("size                        ", info->size);
	serial_field("memory_map_address          ", info->memory_map_address);
	serial_field("memory_map_entries          ", info->memory_map_entries);
	serial_field("framebuffer_address         ", info->framebuffer_address);
	serial_field("framebuffer_width           ", info->framebuffer_width);
	serial_field("framebuffer_height          ", info->framebuffer_height);
	serial_field("framebuffer_pitch           ", info->framebuffer_pitch);
	serial_field("framebuffer_bpp             ", info->framebuffer_bpp);
	serial_field("framebuffer_red_mask_shift  ", info->framebuffer_red_mask_shift);
	serial_field("framebuffer_green_mask_shift", info->framebuffer_green_mask_shift);
	serial_field("framebuffer_blue_mask_shift ", info->framebuffer_blue_mask_shift);
	serial_field("acpi_rsdp_address           ", info->acpi_rsdp_address);
	serial_field("smbios_address              ", info->smbios_address);
}

void kmain(boot_info_t *boot_info)
{
	serial_init();
	dump_boot_info(boot_info);

	uint64_t kernel_start_physical = (uint64_t)_kernel_start;
	uint64_t kernel_end_physical = (uint64_t)_kernel_end;
	uint64_t kernel_size = kernel_end_physical - kernel_start_physical;

	serial_field("Kernel start physical ", kernel_start_physical);
	serial_field("Kernel end physical ", kernel_end_physical);
	serial_field("Kernel size in bytes ", kernel_size);

	physical_memory_manager_init(boot_info);

	heap_init(boot_info);

	acpi_init(boot_info);

	acpi_relocate_tables();
	physical_memory_manager_free_acpi_reclaimable(boot_info);
	physical_memory_manager_lock_protected_regions(boot_info);

	heap_print_stats();

	keyboard_init();

	void* test_alloc1 = kmalloc(10);
	void* test_alloc2 = kmalloc(20);
	void* test_alloc3 = kmalloc(30);

	serial_write("Test allocations successfully completed!\r\n");

	heap_print_stats();

	kfree(test_alloc1);
	kfree(test_alloc2);
	kfree(test_alloc3);

	serial_write("Test free for allocations successfully completed!\r\n");

	heap_print_stats();

	gdt_init();

	idt_init();

	apic_init();

	while(1)
	{
		uint8_t scancode;
		while (keyboard_pop(&scancode))
		{
			serial_write("Key pressed! Scancode: ");
			serial_dec(scancode);
			serial_write("\r\n");
		}

		serial_write("1 one second.\r\n");
		sleep(1000);
	}
}
