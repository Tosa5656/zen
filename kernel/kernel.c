#include "boot_info.h"
#include "memory.h"
#include "serial.h"
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

	uint64_t test_alloc1 = physical_memory_manager_alloc_page();
	uint64_t test_alloc2 = physical_memory_manager_alloc_page();
	uint64_t test_alloc3 = physical_memory_manager_alloc_page();

	serial_field("Test alloc #1 address ", test_alloc1);
	serial_field("Test alloc #2 address ", test_alloc2);
	serial_field("Test alloc #3 address ", test_alloc3);

	physical_memory_manager_free_page(test_alloc1);
	physical_memory_manager_free_page(test_alloc2);
	physical_memory_manager_free_page(test_alloc3);

	idt_init();

	volatile int a = 5;
	volatile int b = 0;
	volatile int c = a / b;
	(void)c;
}
