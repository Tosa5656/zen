#include "memory.h"
#include "buddy.h"
#include "serial.h"
#include "panic.h"

uint64_t boot_alloc_current = 0;

static uint64_t boot_alloc_access_offset = 0;
static int page_table_allocator_is_pmm = 0;
static uint64_t boot_alloc_end = 0;
static uint64_t current_pml4 = 0;

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

void physical_memory_manager_lock_region(uint64_t physical_address, uint64_t size)
{
	buddy_lock_range(physical_address, size);
}

uint64_t physical_memory_manager_alloc_page(void)
{
	return buddy_alloc_page();
}

uint64_t physical_memory_manager_alloc_pages(uint64_t count)
{
	return buddy_alloc_pages(count);
}

void physical_memory_manager_free_page(uint64_t physical_address)
{
	buddy_free_page(physical_address);
}

void physical_memory_manager_free_pages(uint64_t physical_address, uint64_t count)
{
	buddy_free_pages(physical_address, count);
}

uint64_t physical_memory_manager_get_free_pages(void)
{
	return buddy_get_free_pages();
}

void physical_memory_manager_free_region(uint64_t physical_address, uint64_t size)
{
	buddy_free_range(physical_address, size);
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

	serial_field("max physical address", max_physical_address);

	boot_alloc_init();

	uint64_t kernel_size = (uint64_t)_kernel_end - (uint64_t)_kernel_start;
	virtual_memory_manager_init(info, max_physical_address, kernel_size);

	buddy_init(max_physical_address);

	uint64_t reserved_end_page = (boot_alloc_current + PAGE_SIZE - 1) / PAGE_SIZE;

	for (uint64_t i = 0; i < info->memory_map_entries; i++)
	{
		if (memory_map[i].type != MEMORY_TYPE_USABLE)
		{
			continue;
		}

		uint64_t start_page = (memory_map[i].base_address + PAGE_SIZE - 1) / PAGE_SIZE;
		uint64_t end_page = (memory_map[i].base_address + memory_map[i].length) / PAGE_SIZE;

		uint64_t from_page = (start_page > reserved_end_page) ? start_page : reserved_end_page;
		if (from_page >= end_page)
		{
			continue;
		}

		buddy_free_range(from_page * PAGE_SIZE, (end_page - from_page) * PAGE_SIZE);
	}

	serial_field("free pages", buddy_get_free_pages());

	physical_memory_manager_lock_protected_regions(info);

	boot_alloc_end = boot_alloc_current;
	page_table_allocator_is_pmm = 1;

	serial_field("free pages", buddy_get_free_pages());
	serial_write("PMM inited successfully!\r\n");
}

void physical_memory_manager_lock_protected_regions(const boot_info_t *info)
{
	physical_memory_manager_lock_region(0x0, 0x100000);

	uint64_t kernel_physical_start = VIRTUAL_TO_PHYSICAL(_kernel_start);
	physical_memory_manager_lock_region(kernel_physical_start, boot_alloc_current - kernel_physical_start);

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

static uint64_t *table_pointer(uint64_t table_physical)
{
	return (uint64_t *)(table_physical + boot_alloc_access_offset);
}

static uint64_t kernel_pml4_physical(void)
{
	return VIRTUAL_TO_PHYSICAL(&kernel_pml4);
}

static void map_page_in(uint64_t pml4_physical, uint64_t virt, uint64_t physical, uint64_t flags)
{
	size_t pml4_idx = (virt >> 39) & 0x1FF;
	size_t pdpt_idx = (virt >> 30) & 0x1FF;
	size_t pd_idx = (virt >> 21) & 0x1FF;
	size_t pt_idx = (virt >> 12) & 0x1FF;

	uint64_t user = flags & PAGE_USER;

	uint64_t *pml4 = table_pointer(pml4_physical);

	if (!(pml4[pml4_idx] & PAGE_PRESENT))
	{
		pml4[pml4_idx] = allocate_page_table() | PAGE_PRESENT | PAGE_WRITABLE | user;
	}
	else if (user)
	{
		pml4[pml4_idx] |= PAGE_USER;
	}
	uint64_t *pdpt = table_pointer(pml4[pml4_idx] & ~0xFFFULL);

	if (!(pdpt[pdpt_idx] & PAGE_PRESENT))
	{
		pdpt[pdpt_idx] = allocate_page_table() | PAGE_PRESENT | PAGE_WRITABLE | user;
	}
	else if (user)
	{
		pdpt[pdpt_idx] |= PAGE_USER;
	}
	uint64_t *pd = table_pointer(pdpt[pdpt_idx] & ~0xFFFULL);

	if (!(pd[pd_idx] & PAGE_PRESENT))
	{
		pd[pd_idx] = allocate_page_table() | PAGE_PRESENT | PAGE_WRITABLE | user;
	}
	else if (user)
	{
		pd[pd_idx] |= PAGE_USER;
	}
	uint64_t *pt = table_pointer(pd[pd_idx] & ~0xFFFULL);

	pt[pt_idx] = (physical & ~0xFFFULL) | flags | PAGE_PRESENT;
}

void virtual_memory_manager_map_page(uint64_t virt, uint64_t physical, uint64_t flags)
{
	map_page_in(kernel_pml4_physical(), virt, physical, flags);
}

void virtual_memory_manager_map_range(uint64_t virt, uint64_t physical, uint64_t size, uint64_t flags)
{
	for (uint64_t offset = 0; offset < size; offset += PAGE_SIZE)
	{
		virtual_memory_manager_map_page(virt + offset, physical + offset, flags);
	}
}

static uint64_t get_physical_in(uint64_t pml4_physical, uint64_t virt)
{
	size_t pml4_idx = (virt >> 39) & 0x1FF;
	size_t pdpt_idx = (virt >> 30) & 0x1FF;
	size_t pd_idx = (virt >> 21) & 0x1FF;
	size_t pt_idx = (virt >> 12) & 0x1FF;

	uint64_t *pml4 = table_pointer(pml4_physical);

	if (!(pml4[pml4_idx] & PAGE_PRESENT))
	{
		return 0;
	}
	uint64_t *pdpt = table_pointer(pml4[pml4_idx] & ~0xFFFULL);

	if (!(pdpt[pdpt_idx] & PAGE_PRESENT))
	{
		return 0;
	}
	uint64_t *pd = table_pointer(pdpt[pdpt_idx] & ~0xFFFULL);

	if (!(pd[pd_idx] & PAGE_PRESENT))
	{
		return 0;
	}
	uint64_t *pt = table_pointer(pd[pd_idx] & ~0xFFFULL);

	if (!(pt[pt_idx] & PAGE_PRESENT))
	{
		return 0;
	}

	return (pt[pt_idx] & ~0xFFFULL) + (virt & 0xFFF);
}

uint64_t virtual_memory_manager_get_physical(uint64_t virt)
{
	return get_physical_in(kernel_pml4_physical(), virt);
}

uint64_t virtual_memory_manager_translate(uint64_t pml4_physical, uint64_t virt)
{
	return get_physical_in(pml4_physical, virt);
}

static void unmap_page_in(uint64_t pml4_physical, uint64_t virt)
{
	size_t pml4_idx = (virt >> 39) & 0x1FF;
	size_t pdpt_idx = (virt >> 30) & 0x1FF;
	size_t pd_idx = (virt >> 21) & 0x1FF;
	size_t pt_idx = (virt >> 12) & 0x1FF;

	uint64_t *pml4 = table_pointer(pml4_physical);

	if (!(pml4[pml4_idx] & PAGE_PRESENT))
	{
		return;
	}
	uint64_t *pdpt = table_pointer(pml4[pml4_idx] & ~0xFFFULL);

	if (!(pdpt[pdpt_idx] & PAGE_PRESENT))
	{
		return;
	}
	uint64_t *pd = table_pointer(pdpt[pdpt_idx] & ~0xFFFULL);

	if (!(pd[pd_idx] & PAGE_PRESENT))
	{
		return;
	}
	uint64_t *pt = table_pointer(pd[pd_idx] & ~0xFFFULL);

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
		uint64_t pt_physical = pd[pd_idx] & ~0xFFFULL;
		pd[pd_idx] = 0;
		free_page_table_page(pt_physical);

		empty = 1;
		for (int i = 0; i < 512 && empty; i++)
		{
			if (pd[i] & PAGE_PRESENT) empty = 0;
		}
		if (empty)
		{
			uint64_t pd_physical = pdpt[pdpt_idx] & ~0xFFFULL;
			pdpt[pdpt_idx] = 0;
			free_page_table_page(pd_physical);

			empty = 1;
			for (int i = 0; i < 512 && empty; i++)
			{
				if (pdpt[i] & PAGE_PRESENT) empty = 0;
			}
			if (empty)
			{
				uint64_t pdpt_physical = pml4[pml4_idx] & ~0xFFFULL;
				pml4[pml4_idx] = 0;
				free_page_table_page(pdpt_physical);
			}
		}
	}
}

void virtual_memory_manager_unmap_range(uint64_t virt, uint64_t size)
{
	for (uint64_t offset = 0; offset < size; offset += PAGE_SIZE)
	{
		unmap_page_in(kernel_pml4_physical(), virt + offset);
	}
}

void virtual_memory_manager_unmap_user_page(uint64_t pml4_physical, uint64_t virt)
{
	unmap_page_in(pml4_physical, virt);
}

uint64_t virtual_memory_manager_get_kernel_pml4(void)
{
	return kernel_pml4_physical();
}

uint64_t virtual_memory_manager_get_current_pml4(void)
{
	if (current_pml4 == 0)
	{
		return kernel_pml4_physical();
	}

	return current_pml4;
}

uint64_t virtual_memory_manager_create_address_space(void)
{
	uint64_t pml4_physical = physical_memory_manager_alloc_page();
	if (pml4_physical == 0)
	{
		return 0;
	}

	uint64_t *new_pml4 = table_pointer(pml4_physical);
	for (int i = 0; i < 512; i++)
	{
		new_pml4[i] = 0;
	}

	uint64_t *kernel_table = table_pointer(kernel_pml4_physical());
	for (int i = 256; i < 512; i++)
	{
		new_pml4[i] = kernel_table[i];
	}

	return pml4_physical;
}

void virtual_memory_manager_destroy_address_space(uint64_t pml4_physical)
{
	if (pml4_physical == 0 || pml4_physical == kernel_pml4_physical())
	{
		return;
	}

	uint64_t *pml4 = table_pointer(pml4_physical);

	for (int pml4_idx = 0; pml4_idx < 256; pml4_idx++)
	{
		if (!(pml4[pml4_idx] & PAGE_PRESENT))
		{
			continue;
		}
		uint64_t *pdpt = table_pointer(pml4[pml4_idx] & ~0xFFFULL);

		for (int pdpt_idx = 0; pdpt_idx < 512; pdpt_idx++)
		{
			if (!(pdpt[pdpt_idx] & PAGE_PRESENT))
			{
				continue;
			}
			uint64_t *pd = table_pointer(pdpt[pdpt_idx] & ~0xFFFULL);

			for (int pd_idx = 0; pd_idx < 512; pd_idx++)
			{
				if (!(pd[pd_idx] & PAGE_PRESENT))
				{
					continue;
				}
				if (pd[pd_idx] & PAGE_HUGE)
				{
					pd[pd_idx] = 0;
					continue;
				}
				uint64_t *pt = table_pointer(pd[pd_idx] & ~0xFFFULL);

				for (int pt_idx = 0; pt_idx < 512; pt_idx++)
				{
					if (!(pt[pt_idx] & PAGE_PRESENT))
					{
						continue;
					}

					physical_memory_manager_free_page(pt[pt_idx] & ~0xFFFULL);
					pt[pt_idx] = 0;
				}

				free_page_table_page(pd[pd_idx] & ~0xFFFULL);
				pd[pd_idx] = 0;
			}

			free_page_table_page(pdpt[pdpt_idx] & ~0xFFFULL);
			pdpt[pdpt_idx] = 0;
		}

		free_page_table_page(pml4[pml4_idx] & ~0xFFFULL);
		pml4[pml4_idx] = 0;
	}

	physical_memory_manager_free_page(pml4_physical);
}

void virtual_memory_manager_switch_address_space(uint64_t pml4_physical)
{
	if (pml4_physical == 0)
	{
		return;
	}

	current_pml4 = pml4_physical;
	__asm__ volatile("mov %0, %%cr3" :: "r"(pml4_physical) : "memory");
}

int virtual_memory_manager_map_user_page(uint64_t pml4_physical, uint64_t virt, uint64_t physical, uint64_t flags)
{
	if (pml4_physical == 0 || virt >= USER_SPACE_END)
	{
		return 0;
	}

	map_page_in(pml4_physical, virt, physical, (flags & ~PAGE_HUGE) | PAGE_USER);
	return 1;
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
	uint64_t identity_end = (boot_alloc_current > 0x400000) ? boot_alloc_current : 0x400000;
	identity_end = (identity_end + 0x1FFFFFULL) & ~0x1FFFFFULL;

	for (uint64_t address = 0; address < identity_end; address += PAGE_SIZE)
	{
		virtual_memory_manager_map_page(address, address, PAGE_WRITABLE);
	}

	for (uint64_t address = 0; address < max_ram; address += PAGE_SIZE)
	{
		virtual_memory_manager_map_page(KERNEL_PHYSICAL_MAP_BASE + address, address, PAGE_WRITABLE);
	}

	for (uint64_t offset = 0; offset < kernel_size; offset += PAGE_SIZE)
	{
		virtual_memory_manager_map_page(KERNEL_IMAGE_VIRTUAL + offset, 0x100000 + offset, PAGE_WRITABLE);
	}

	uint64_t framebuffer_physical = info->framebuffer_address;
	uint64_t framebuffer_size = info->framebuffer_height * info->framebuffer_pitch;

	for (uint64_t offset = 0; offset < framebuffer_size; offset += PAGE_SIZE)
	{
		virtual_memory_manager_map_page(KERNEL_MMIO_BASE + framebuffer_physical + offset,
			framebuffer_physical + offset, PAGE_WRITABLE | PAGE_CACHE_DISABLE);
	}

	uint64_t pml4_phys = VIRTUAL_TO_PHYSICAL(&kernel_pml4);
	__asm__ volatile("mov %0, %%cr3" :: "r"(pml4_phys) : "memory");

	boot_alloc_access_offset = KERNEL_PHYSICAL_MAP_BASE;

	serial_write("CR3 updated, VMM inited!\r\n");
}

void boot_alloc_init(void)
{
	if (boot_alloc_current != 0)
	{
		return;
	}

	uint64_t raw_end = VIRTUAL_TO_PHYSICAL(_kernel_end);
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
