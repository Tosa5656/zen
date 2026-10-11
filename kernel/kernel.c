#include "boot_info.h"
#include "memory.h"
#include "serial.h"

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

	find_maximum_physical_memory(boot_info);
	physical_memory_manager_init(boot_info);
}
