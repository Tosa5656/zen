#include "boot_info.h"
#include "memory.h"
#include "serial.h"
#include "timer.h"
#include "acpi.h"
#include "apic.h"
#include "gdt.h"
#include "heap.h"
#include "slab.h"
#include "buddy.h"
#include "keyboard.h"
#include "syscall.h"
#include "usertest.h"
#include "idt.h"
#include "scheduler.h"
#include "ata.h"
#include "vfs.h"
#include "fd.h"
#include "fs/fat32.h"

extern uint8_t _kernel_start[];
extern uint8_t _kernel_end[];

static void dump_boot_info(const boot_info_t *info)
{
	if (info == 0)
	{
		serial_write("boot_info pointer is NULL!\r\n");
		return;
	}

	serial_field("magic", info->magic);
	serial_field("size", info->size);
	serial_field("memory_map_address", info->memory_map_address);
	serial_field("memory_map_entries", info->memory_map_entries);
	serial_field("framebuffer_address", info->framebuffer_address);
	serial_field("framebuffer_width", info->framebuffer_width);
	serial_field("framebuffer_height", info->framebuffer_height);
	serial_field("framebuffer_pitch", info->framebuffer_pitch);
	serial_field("framebuffer_bpp", info->framebuffer_bpp);
	serial_field("framebuffer_red_mask_shift", info->framebuffer_red_mask_shift);
	serial_field("framebuffer_green_mask_shift", info->framebuffer_green_mask_shift);
	serial_field("framebuffer_blue_mask_shift", info->framebuffer_blue_mask_shift);
	serial_field("acpi_rsdp_address", info->acpi_rsdp_address);
	serial_field("smbios_address", info->smbios_address);
}

void kmain(boot_info_t *boot_info)
{
	serial_init();
	dump_boot_info(boot_info);

	uint64_t kernel_start_physical = VIRTUAL_TO_PHYSICAL(_kernel_start);
	uint64_t kernel_end_physical = VIRTUAL_TO_PHYSICAL(_kernel_end);
	uint64_t kernel_size = kernel_end_physical - kernel_start_physical;

	serial_field("kernel start physical", kernel_start_physical);
	serial_field("kernel end physical", kernel_end_physical);
	serial_field("kernel size in bytes", kernel_size);

	physical_memory_manager_init(boot_info);

	heap_init(boot_info);

	slab_init();

	vfs_init();

	acpi_init(boot_info);

	acpi_relocate_tables();
	physical_memory_manager_free_acpi_reclaimable(boot_info);
	physical_memory_manager_lock_protected_regions(boot_info);

	heap_print_stats();

	keyboard_init();

	slab_print_details();
	buddy_print_stats();

	gdt_init();

	idt_init();

	apic_init();

	scheduler_init();

	syscall_init();

	ata_init();

	fat32_init();
	fat32_mount_all();
	fat32_list_first();

	usermode_test();

	__asm__ volatile("sti");

	while(1)
	{
		uint8_t scancode;
		while (keyboard_pop(&scancode))
		{
			serial_write("Key pressed! Scancode = ");
			serial_dec(scancode);
			serial_write("\r\n");
		}
	}
}
