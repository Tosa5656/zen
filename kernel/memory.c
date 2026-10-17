#include "memory.h"
#include "serial.h"

uint8_t* physical_memory_manager_bitmap = NULL;
uint64_t physical_memory_manager_bitmap_size = 0;
uint64_t total_pages = 0;
uint64_t boot_alloc_current = 0;

static uint64_t boot_alloc_access_offset = 0;

__attribute__((aligned(4096))) pml4_table_t kernel_pml4 = {0};

uint64_t physical_memory_manager_get_max_ram(const boot_info_t *info)
{
	if (info == NULL || info->memory_map_address == 0)
	{
		return 0;
	}

	memory_map_entry_t *memory_map = (memory_map_entry_t*)info->memory_map_address;
	uint64_t max_physical_address = 0;

	for (uint64_t i = 0; i < info->memory_map_entries; i++)
	{
		if (memory_map[i].type != MEMORY_TYPE_USABLE)
		{
			continue;
		}

		uint64_t region_end = memory_map[i].base_address + memory_map[i].length;
		if (region_end > max_physical_address)
		{
			max_physical_address = region_end;
		}
	}

	return max_physical_address;
}

void physical_memory_manager_set_page_used(uint64_t page_index)
{
	physical_memory_manager_bitmap[page_index / 8] |= (1 << (page_index % 8));
}

void physical_memory_manager_set_page_free(uint64_t page_index)
{
	physical_memory_manager_bitmap[page_index / 8] &= ~(1 << (page_index % 8));
}

void physical_memory_manager_lock_region(uint64_t physical_address, uint64_t size)
{
	uint64_t start_page = physical_address / PAGE_SIZE;
	uint64_t end_page = (physical_address + size + PAGE_SIZE - 1) / PAGE_SIZE;

	for (uint64_t page = start_page; page < end_page; page++)
	{
		if (page < total_pages)
		{
			physical_memory_manager_set_page_used(page);
		}
	}
}

uint64_t physical_memory_manager_alloc_page(void)
{
	for (uint64_t i = 0; i < physical_memory_manager_bitmap_size; i++)
	{
		if (physical_memory_manager_bitmap[i] == 0xFF)
		{
			continue;
		}

		for (int bit = 0; bit < 8; bit++)
		{
			uint64_t page_index = i * 8 + bit;

			if (page_index >= total_pages)
			{
				return 0;
			}

			if (!(physical_memory_manager_bitmap[i] & (1 << bit)))
			{
				physical_memory_manager_set_page_used(page_index);

				uint64_t physical_address = page_index * PAGE_SIZE;
				return physical_address;
			}
		}
	}

	return 0;
}

void physical_memory_manager_free_page(uint64_t physical_address)
{
	uint64_t page_index = physical_address / PAGE_SIZE;

	if (page_index < total_pages)
	{
		physical_memory_manager_set_page_free(page_index);
	}
}

void physical_memory_manager_free_region(uint64_t physical_address, uint64_t size)
{
	uint64_t start_page = physical_address / PAGE_SIZE;
	uint64_t end_page = (physical_address + size + PAGE_SIZE - 1) / PAGE_SIZE;

	for (uint64_t page = start_page; page < end_page && page < total_pages; page++)
	{
		physical_memory_manager_set_page_free(page);
	}
}

void physical_memory_manager_free_acpi_reclaimable(const boot_info_t *info)
{
	if (info == NULL || info->memory_map_address == 0)
	{
		return;
	}

	memory_map_entry_t *memory_map = (memory_map_entry_t*)info->memory_map_address;

	for (uint64_t i = 0; i < info->memory_map_entries; i++)
	{
		if (memory_map[i].type != MEMORY_TYPE_ACPI_RECLAIMABLE)
		{
			continue;
		}

		physical_memory_manager_free_region(memory_map[i].base_address, memory_map[i].length);
	}
}

void physical_memory_manager_init(const boot_info_t *info)
{
	memory_map_entry_t *memory_map = (memory_map_entry_t*)info->memory_map_address;
	uint64_t max_physical_address = physical_memory_manager_get_max_ram(info);

	if (max_physical_address == 0)
	{
		serial_write("Error! Maximum RAM is 0!\n");
		return;
	}

	total_pages = max_physical_address / PAGE_SIZE;
	physical_memory_manager_bitmap_size = total_pages / 8;
	if (total_pages % 8 != 0) physical_memory_manager_bitmap_size++;

	uint64_t bitmap_physical_address = 0;
	for(uint64_t i = 0; i < info->memory_map_entries; i++)
	{
		if(memory_map[i].type != MEMORY_TYPE_USABLE)
		{
			continue;
		}

		uint64_t region_start = memory_map[i].base_address;
		uint64_t region_end = region_start + memory_map[i].length;

		if(PHYSICAL_MEMORY_MANAGER_BITMAP_BASE >= region_start && 
		   PHYSICAL_MEMORY_MANAGER_BITMAP_BASE + physical_memory_manager_bitmap_size <= region_end)
		{
			bitmap_physical_address = PHYSICAL_MEMORY_MANAGER_BITMAP_BASE;
			break;
		}
	}

	if(bitmap_physical_address == 0)
	{
		serial_write("Could not find free RAM for PMM Bitmap!\n");
		return;
	}

	physical_memory_manager_bitmap = (uint8_t*)bitmap_physical_address;

	serial_field("Max physical address: ", max_physical_address);
	serial_field("Total pages total: ", total_pages);
	serial_field("Bitmap size: ", physical_memory_manager_bitmap_size);
	serial_field("Bitmap phys allocation address: ", bitmap_physical_address);

	for (uint64_t i = 0; i < physical_memory_manager_bitmap_size; i++)
	{
		physical_memory_manager_bitmap[i] = 0xFF;
	}

	uint64_t kernel_size = (uint64_t)_kernel_end - (uint64_t)_kernel_start;
	virtual_memory_manager_init(info, max_physical_address, kernel_size);

	physical_memory_manager_bitmap = (uint8_t*)(bitmap_physical_address + KERNEL_PHYSICAL_MAP_BASE);

	for (uint64_t i = 0; i < info->memory_map_entries; i++)
	{
		if (memory_map[i].type == MEMORY_TYPE_USABLE)
		{
			uint64_t start_page = memory_map[i].base_address / PAGE_SIZE;
			uint64_t end_page = (memory_map[i].base_address + memory_map[i].length) / PAGE_SIZE;

			for (uint64_t page = start_page; page < end_page; page++)
			{
				if (page < total_pages)
				{
					physical_memory_manager_set_page_free(page);
				}
			}
		}
	}

	physical_memory_manager_lock_region(0x0, 0x100000);

	uint64_t kernel_physical_start = (uint64_t)_kernel_start;
	uint64_t dynamic_end = boot_alloc_current;
	uint64_t total_kernel_and_tables_size = dynamic_end - kernel_physical_start;

	physical_memory_manager_lock_region(kernel_physical_start, total_kernel_and_tables_size);
	physical_memory_manager_lock_region(bitmap_physical_address, physical_memory_manager_bitmap_size);

	physical_memory_manager_lock_region((uint64_t)info, info->size);
	physical_memory_manager_lock_region(info->memory_map_address, info->memory_map_entries * sizeof(memory_map_entry_t));

	serial_write("PMM inited successfully!\r\n");
}

void virtual_memory_manager_map_page(uint64_t virt, uint64_t physical, uint64_t flags)
{
	size_t pml4_idx = (virt >> 39) & 0x1FF;
	size_t pdpt_idx = (virt >> 30) & 0x1FF;
	size_t pd_idx = (virt >> 21) & 0x1FF;
	size_t pt_idx = (virt >> 12) & 0x1FF;

	if (!(kernel_pml4[pml4_idx] & PAGE_PRESENT))
	{
		uint64_t new_table = allocate_boot_page();
		kernel_pml4[pml4_idx] = new_table | PAGE_PRESENT | PAGE_WRITABLE;
	}
	uint64_t* pdpt = (uint64_t*)((kernel_pml4[pml4_idx] & ~0xFFF) + boot_alloc_access_offset);

	if (!(pdpt[pdpt_idx] & PAGE_PRESENT))
	{
		uint64_t new_table = allocate_boot_page();
		pdpt[pdpt_idx] = new_table | PAGE_PRESENT | PAGE_WRITABLE;
	}
	uint64_t* pd = (uint64_t*)((pdpt[pdpt_idx] & ~0xFFF) + boot_alloc_access_offset);

	if (!(pd[pd_idx] & PAGE_PRESENT))
	{
		uint64_t new_table = allocate_boot_page();
		pd[pd_idx] = new_table | PAGE_PRESENT | PAGE_WRITABLE;
	}
	uint64_t* pt = (uint64_t*)((pd[pd_idx] & ~0xFFF) + boot_alloc_access_offset);

	pt[pt_idx] = (physical & ~0xFFF) | flags | PAGE_PRESENT;
}

void virtual_memory_manager_init(const boot_info_t *info, uint64_t max_ram, uint64_t kernel_size)
{
	boot_alloc_init();

	for (uint64_t address = 0; address < 0x400000; address += PAGE_SIZE)
	{
		virtual_memory_manager_map_page(address, address, PAGE_WRITABLE);
	}

	for (uint64_t address = 0; address < max_ram; address += PAGE_SIZE)
	{
		virtual_memory_manager_map_page(KERNEL_PHYSICAL_MAP_BASE + address, address, PAGE_WRITABLE);
	}

	for (uint64_t offset = 0; offset < kernel_size; offset += PAGE_SIZE)
	{
		virtual_memory_manager_map_page(0xFFFFFFFF80000000ULL + offset, 0x100000 + offset, PAGE_WRITABLE);
	}

	uint64_t framebuffer_physical = info->framebuffer_address;
	uint64_t framebuffer_size = info->framebuffer_height * info->framebuffer_pitch;

	for (uint64_t offset = 0; offset < framebuffer_size; offset += PAGE_SIZE)
	{
		virtual_memory_manager_map_page(framebuffer_physical + offset, framebuffer_physical + offset, PAGE_WRITABLE);
	}

	uint64_t pml4_phys = (uint64_t)&kernel_pml4;
	__asm__ volatile("mov %0, %%cr3" :: "r"(pml4_phys) : "memory");

	boot_alloc_access_offset = KERNEL_PHYSICAL_MAP_BASE;

	serial_write("CR3 updated, VMM inited!\r\n");
}

void boot_alloc_init(void)
{
	uint64_t raw_end = (uint64_t)&_kernel_end;
	boot_alloc_current = (raw_end + 4095) & ~4095;
}

uint64_t allocate_boot_page(void)
{
	uint64_t allocated_page = boot_alloc_current;
	boot_alloc_current += 4096;

	uint64_t* ptr = (uint64_t*)(allocated_page + boot_alloc_access_offset);
	for (int i = 0; i < 512; i++)
	{
		ptr[i] = 0;
	}

	return allocated_page;
}
