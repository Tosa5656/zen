#include "memory.h"
#include "serial.h"
#include "panic.h"

uint8_t* physical_memory_manager_bitmap = NULL;
uint64_t physical_memory_manager_bitmap_size = 0;
uint64_t total_pages = 0;
uint64_t boot_alloc_current = 0;

static uint64_t boot_alloc_access_offset = 0;
static uint64_t free_pages = 0;
static uint64_t next_free_hint = 0;
static int page_table_allocator_is_pmm = 0;
static uint64_t boot_alloc_end = 0;

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
	uint64_t byte = page_index / 8;
	uint8_t mask = (uint8_t)(1 << (page_index % 8));

	if (physical_memory_manager_bitmap[byte] & mask)
	{
		return;
	}

	physical_memory_manager_bitmap[byte] |= mask;
	free_pages--;
}

void physical_memory_manager_set_page_free(uint64_t page_index)
{
	uint64_t byte = page_index / 8;
	uint8_t mask = (uint8_t)(1 << (page_index % 8));

	if (!(physical_memory_manager_bitmap[byte] & mask))
	{
		return;
	}

	physical_memory_manager_bitmap[byte] &= (uint8_t)~mask;
	free_pages++;
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

static int physical_memory_manager_page_is_free(uint64_t page_index)
{
	if (page_index >= total_pages)
	{
		return 0;
	}

	uint64_t byte = page_index / 8;
	uint8_t mask = (uint8_t)(1 << (page_index % 8));

	return (physical_memory_manager_bitmap[byte] & mask) == 0;
}

static int64_t physical_memory_manager_find_run(uint64_t count, uint64_t begin, uint64_t end)
{
	uint64_t run = 0;

	for (uint64_t page = begin; page < end; page++)
	{
		if (physical_memory_manager_page_is_free(page))
		{
			run++;
			if (run == count)
			{
				return (int64_t)(page - count + 1);
			}
		}
		else
		{
			run = 0;
		}
	}

	return -1;
}

uint64_t physical_memory_manager_alloc_page(void)
{
	return physical_memory_manager_alloc_pages(1);
}

uint64_t physical_memory_manager_alloc_pages(uint64_t count)
{
	if (physical_memory_manager_bitmap == NULL || count == 0 || count > free_pages)
	{
		return 0;
	}

	uint64_t hint = (next_free_hint < total_pages) ? next_free_hint : 0;

	int64_t first = physical_memory_manager_find_run(count, hint, total_pages);
	if (first < 0)
	{
		first = physical_memory_manager_find_run(count, 0, total_pages);
	}

	if (first < 0)
	{
		return 0;
	}

	uint64_t first_page = (uint64_t)first;

	for (uint64_t page = first_page; page < first_page + count; page++)
	{
		physical_memory_manager_set_page_used(page);
	}

	next_free_hint = first_page + count;

	return first_page * PAGE_SIZE;
}

void physical_memory_manager_free_page(uint64_t physical_address)
{
	uint64_t page_index = physical_address / PAGE_SIZE;

	if (page_index < total_pages)
	{
		physical_memory_manager_set_page_free(page_index);
	}
}

void physical_memory_manager_free_pages(uint64_t physical_address, uint64_t count)
{
	uint64_t first_page = physical_address / PAGE_SIZE;

	for (uint64_t page = first_page; page < first_page + count && page < total_pages; page++)
	{
		physical_memory_manager_set_page_free(page);
	}

	if (first_page < next_free_hint)
	{
		next_free_hint = first_page;
	}
}

uint64_t physical_memory_manager_get_free_pages(void)
{
	return free_pages;
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

	physical_memory_manager_lock_protected_regions(info);

	boot_alloc_end = boot_alloc_current;
	page_table_allocator_is_pmm = 1;

	serial_field("Free pages ", free_pages);
	serial_write("PMM inited successfully!\r\n");
}

void physical_memory_manager_lock_protected_regions(const boot_info_t *info)
{
	physical_memory_manager_lock_region(0x0, 0x100000);

	uint64_t kernel_physical_start = (uint64_t)_kernel_start;
	physical_memory_manager_lock_region(kernel_physical_start, boot_alloc_current - kernel_physical_start);

	if (physical_memory_manager_bitmap != NULL)
	{
		uint64_t bitmap_physical_address =
			(uint64_t)physical_memory_manager_bitmap - KERNEL_PHYSICAL_MAP_BASE;

		physical_memory_manager_lock_region(bitmap_physical_address, physical_memory_manager_bitmap_size);
	}

	physical_memory_manager_lock_region((uint64_t)info, info->size);
	physical_memory_manager_lock_region(info->memory_map_address,
		info->memory_map_entries * sizeof(memory_map_entry_t));
}

static uint64_t allocate_page_table(void)
{
	if (!page_table_allocator_is_pmm)
	{
		return allocate_boot_page();
	}

	uint64_t physical = physical_memory_manager_alloc_page();
	if (physical == 0)
	{
		kernel_panic("VMM: out of memory while allocating a page table");
		return 0;
	}

	uint64_t *table = (uint64_t *)(physical + boot_alloc_access_offset);
	for (int i = 0; i < 512; i++)
	{
		table[i] = 0;
	}

	return physical;
}

static void free_page_table_page(uint64_t physical)
{
	if (!page_table_allocator_is_pmm || physical < boot_alloc_end)
	{
		return;
	}

	physical_memory_manager_free_page(physical);
}

void virtual_memory_manager_map_page(uint64_t virt, uint64_t physical, uint64_t flags)
{
	size_t pml4_idx = (virt >> 39) & 0x1FF;
	size_t pdpt_idx = (virt >> 30) & 0x1FF;
	size_t pd_idx = (virt >> 21) & 0x1FF;
	size_t pt_idx = (virt >> 12) & 0x1FF;

	if (!(kernel_pml4[pml4_idx] & PAGE_PRESENT))
	{
		uint64_t new_table = allocate_page_table();
		kernel_pml4[pml4_idx] = new_table | PAGE_PRESENT | PAGE_WRITABLE;
	}
	uint64_t* pdpt = (uint64_t*)((kernel_pml4[pml4_idx] & ~0xFFF) + boot_alloc_access_offset);

	if (!(pdpt[pdpt_idx] & PAGE_PRESENT))
	{
		uint64_t new_table = allocate_page_table();
		pdpt[pdpt_idx] = new_table | PAGE_PRESENT | PAGE_WRITABLE;
	}
	uint64_t* pd = (uint64_t*)((pdpt[pdpt_idx] & ~0xFFF) + boot_alloc_access_offset);

	if (!(pd[pd_idx] & PAGE_PRESENT))
	{
		uint64_t new_table = allocate_page_table();
		pd[pd_idx] = new_table | PAGE_PRESENT | PAGE_WRITABLE;
	}
	uint64_t* pt = (uint64_t*)((pd[pd_idx] & ~0xFFF) + boot_alloc_access_offset);

	pt[pt_idx] = (physical & ~0xFFF) | flags | PAGE_PRESENT;
}

void virtual_memory_manager_map_range(uint64_t virt, uint64_t physical, uint64_t size, uint64_t flags)
{
	for (uint64_t offset = 0; offset < size; offset += PAGE_SIZE)
	{
		virtual_memory_manager_map_page(virt + offset, physical + offset, flags);
	}
}

uint64_t virtual_memory_manager_get_physical(uint64_t virt)
{
	size_t pml4_idx = (virt >> 39) & 0x1FF;
	size_t pdpt_idx = (virt >> 30) & 0x1FF;
	size_t pd_idx = (virt >> 21) & 0x1FF;
	size_t pt_idx = (virt >> 12) & 0x1FF;

	if (!(kernel_pml4[pml4_idx] & PAGE_PRESENT))
	{
		return 0;
	}
	uint64_t* pdpt = (uint64_t*)((kernel_pml4[pml4_idx] & ~0xFFF) + boot_alloc_access_offset);

	if (!(pdpt[pdpt_idx] & PAGE_PRESENT))
	{
		return 0;
	}
	uint64_t* pd = (uint64_t*)((pdpt[pdpt_idx] & ~0xFFF) + boot_alloc_access_offset);

	if (!(pd[pd_idx] & PAGE_PRESENT))
	{
		return 0;
	}
	uint64_t* pt = (uint64_t*)((pd[pd_idx] & ~0xFFF) + boot_alloc_access_offset);

	if (!(pt[pt_idx] & PAGE_PRESENT))
	{
		return 0;
	}

	return (pt[pt_idx] & ~0xFFF) + (virt & 0xFFF);
}

static void virtual_memory_manager_unmap_page(uint64_t virt)
{
	size_t pml4_idx = (virt >> 39) & 0x1FF;
	size_t pdpt_idx = (virt >> 30) & 0x1FF;
	size_t pd_idx = (virt >> 21) & 0x1FF;
	size_t pt_idx = (virt >> 12) & 0x1FF;

	if (!(kernel_pml4[pml4_idx] & PAGE_PRESENT))
	{
		return;
	}
	uint64_t* pdpt = (uint64_t*)((kernel_pml4[pml4_idx] & ~0xFFF) + boot_alloc_access_offset);

	if (!(pdpt[pdpt_idx] & PAGE_PRESENT))
	{
		return;
	}
	uint64_t* pd = (uint64_t*)((pdpt[pdpt_idx] & ~0xFFF) + boot_alloc_access_offset);

	if (!(pd[pd_idx] & PAGE_PRESENT))
	{
		return;
	}
	uint64_t* pt = (uint64_t*)((pd[pd_idx] & ~0xFFF) + boot_alloc_access_offset);

	if (!(pt[pt_idx] & PAGE_PRESENT))
	{
		return;
	}

	pt[pt_idx] = 0;
	__asm__ volatile("invlpg (%0)" :: "r"(virt) : "memory");

	int empty = 1;
	for (int i = 0; i < 512 && empty; i++)
	{
		if (pt[i] & PAGE_PRESENT) empty = 0;
	}
	if (empty)
	{
		uint64_t pt_physical = pd[pd_idx] & ~0xFFF;
		pd[pd_idx] = 0;
		free_page_table_page(pt_physical);

		empty = 1;
		for (int i = 0; i < 512 && empty; i++)
		{
			if (pd[i] & PAGE_PRESENT) empty = 0;
		}
		if (empty)
		{
			uint64_t pd_physical = pdpt[pdpt_idx] & ~0xFFF;
			pdpt[pdpt_idx] = 0;
			free_page_table_page(pd_physical);

			empty = 1;
			for (int i = 0; i < 512 && empty; i++)
			{
				if (pdpt[i] & PAGE_PRESENT) empty = 0;
			}
			if (empty)
			{
				uint64_t pdpt_physical = kernel_pml4[pml4_idx] & ~0xFFF;
				kernel_pml4[pml4_idx] = 0;
				free_page_table_page(pdpt_physical);
			}
		}
	}
}

void virtual_memory_manager_unmap_range(uint64_t virt, uint64_t size)
{
	for (uint64_t offset = 0; offset < size; offset += PAGE_SIZE)
	{
		virtual_memory_manager_unmap_page(virt + offset);
	}
}

uint64_t virtual_memory_manager_map_anonymous(uint64_t virt, uint64_t size, uint64_t flags)
{
	uint64_t mapped = 0;

	while (mapped < size)
	{
		uint64_t physical = physical_memory_manager_alloc_page();
		if (physical == 0)
		{
			break;
		}

		virtual_memory_manager_map_page(virt + mapped, physical, flags);
		mapped += PAGE_SIZE;
	}

	if (mapped != size)
	{
		for (uint64_t offset = 0; offset < mapped; offset += PAGE_SIZE)
		{
			uint64_t physical = virtual_memory_manager_get_physical(virt + offset);
			if (physical != 0)
			{
				physical_memory_manager_free_page(physical);
			}
		}

		virtual_memory_manager_unmap_range(virt, mapped);
		return 0;
	}

	return virt;
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
