#include "buddy.h"
#include "memory.h"
#include "panic.h"
#include "serial.h"

#define META_USED (1u << 7)
#define META_SLAB (1u << 6)
#define META_START (1u << 5)
#define META_ORDER_MASK 0x0Fu
#define META_NO_BLOCK (BUDDY_MAX_ORDER + 1)

typedef struct buddy_block
{
	struct buddy_block *prev;
	struct buddy_block *next;
} buddy_block_t;

static uint8_t *buddy_meta = 0;
static uint64_t buddy_total_pages = 0;
static uint64_t buddy_free_count = 0;
static buddy_block_t *buddy_free_lists[BUDDY_MAX_ORDER + 1];

static void *buddy_page_pointer(uint64_t page)
{
	return (void *)(KERNEL_PHYSICAL_MAP_BASE + page * PAGE_SIZE);
}

static uint64_t buddy_pointer_page(const void *pointer)
{
	return ((uint64_t)pointer - KERNEL_PHYSICAL_MAP_BASE) / PAGE_SIZE;
}

static void buddy_list_push(uint64_t order, buddy_block_t *block)
{
	block->prev = 0;
	block->next = buddy_free_lists[order];

	if (block->next != 0)
	{
		block->next->prev = block;
	}

	buddy_free_lists[order] = block;
}

static void buddy_list_remove(uint64_t order, buddy_block_t *block)
{
	if (block->prev != 0)
	{
		block->prev->next = block->next;
	}
	else
	{
		buddy_free_lists[order] = block->next;
	}

	if (block->next != 0)
	{
		block->next->prev = block->prev;
	}

	block->prev = 0;
	block->next = 0;
}

static uint64_t buddy_containing_order(uint64_t page)
{
	for (uint64_t order = BUDDY_MAX_ORDER + 1; order-- > 0;)
	{
		uint64_t pages = (uint64_t)1 << order;
		uint64_t start = page & ~(pages - 1);
		uint8_t meta = buddy_meta[start];

		if ((meta & (META_USED | META_START)) == META_START &&
			(uint64_t)(meta & META_ORDER_MASK) == order)
		{
			return order;
		}
	}

	return META_NO_BLOCK;
}

static void buddy_free_block(uint64_t page, uint64_t order)
{
	if (page >= buddy_total_pages || (buddy_meta[page] & META_USED) == 0)
	{
		return;
	}

	while (order < BUDDY_MAX_ORDER)
	{
		uint64_t buddy_page = page ^ ((uint64_t)1 << order);
		if (buddy_page >= buddy_total_pages)
		{
			break;
		}

		uint8_t meta = buddy_meta[buddy_page];
		if ((meta & (META_USED | META_START)) != META_START)
		{
			break;
		}

		if ((uint64_t)(meta & META_ORDER_MASK) != order)
		{
			break;
		}

		buddy_list_remove(order, buddy_page_pointer(buddy_page));
		buddy_free_count -= (uint64_t)1 << order;

		if (buddy_page < page)
		{
			page = buddy_page;
		}

		order++;
	}

	uint64_t pages = (uint64_t)1 << order;

	for (uint64_t i = 0; i < pages; i++)
	{
		buddy_meta[page + i] = 0;
	}

	buddy_meta[page] = (uint8_t)(META_START | order);
	buddy_list_push(order, buddy_page_pointer(page));
	buddy_free_count += pages;
}

static int buddy_block_is_freeable(uint64_t page, uint64_t pages)
{
	if (page + pages > buddy_total_pages)
	{
		return 0;
	}

	for (uint64_t i = 0; i < pages; i++)
	{
		if ((buddy_meta[page + i] & META_USED) == 0)
		{
			return 0;
		}
	}

	return 1;
}

static void buddy_push_range(uint64_t from, uint64_t to)
{
	if (to > buddy_total_pages)
	{
		to = buddy_total_pages;
	}

	while (from < to)
	{
		uint64_t order = 0;

		while (order < BUDDY_MAX_ORDER)
		{
			uint64_t pages = (uint64_t)1 << (order + 1);
			if ((from & (pages - 1)) != 0)
			{
				break;
			}
			if (from + pages > to)
			{
				break;
			}
			if (!buddy_block_is_freeable(from, pages))
			{
				break;
			}

			order++;
		}

		if (buddy_block_is_freeable(from, (uint64_t)1 << order))
		{
			buddy_free_block(from, order);
		}

		from += (uint64_t)1 << order;
	}
}

void buddy_init(uint64_t max_physical_address)
{
	buddy_total_pages = max_physical_address / PAGE_SIZE;
	buddy_free_count = 0;

	for (uint64_t i = 0; i <= BUDDY_MAX_ORDER; i++)
	{
		buddy_free_lists[i] = 0;
	}

	if (buddy_total_pages == 0)
	{
		kernel_panic_value("buddy: no physical memory", max_physical_address);
	}

	uint64_t meta_pages = (buddy_total_pages + PAGE_SIZE - 1) / PAGE_SIZE;
	uint64_t meta_physical = 0;

	for (uint64_t i = 0; i < meta_pages; i++)
	{
		uint64_t page = allocate_boot_page();
		if (page == 0)
		{
			kernel_panic("buddy: out of memory while reserving metadata");
		}

		if (i == 0)
		{
			meta_physical = page;
		}
	}

	buddy_meta = (uint8_t *)buddy_page_pointer(meta_physical / PAGE_SIZE);

	for (uint64_t i = 0; i < buddy_total_pages; i++)
	{
		buddy_meta[i] = META_USED;
	}

	serial_field("buddy pages", buddy_total_pages);
	serial_field("buddy metadata", meta_pages);
	serial_field("buddy meta", meta_physical);
}

uint64_t buddy_alloc_pages(uint64_t count)
{
	if (buddy_meta == 0 || count == 0 || count > buddy_free_count)
	{
		return 0;
	}

	uint64_t order = 0;
	while (((uint64_t)1 << order) < count)
	{
		order++;
		if (order > BUDDY_MAX_ORDER)
		{
			return 0;
		}
	}

	uint64_t found = order;
	while (found <= BUDDY_MAX_ORDER && buddy_free_lists[found] == 0)
	{
		found++;
	}

	if (found > BUDDY_MAX_ORDER)
	{
		return 0;
	}

	buddy_block_t *block = buddy_free_lists[found];
	buddy_list_remove(found, block);
	buddy_free_count -= (uint64_t)1 << found;

	uint64_t page = buddy_pointer_page(block);

	while (found > order)
	{
		found--;
		uint64_t half = (uint64_t)1 << found;
		uint64_t second = page + half;

		buddy_meta[second] = (uint8_t)(META_START | found);
		buddy_list_push(found, buddy_page_pointer(second));
		buddy_free_count += half;
	}

	for (uint64_t i = 0; i < ((uint64_t)1 << order); i++)
	{
		buddy_meta[page + i] = META_USED;
	}

	return page * PAGE_SIZE;
}

uint64_t buddy_alloc_page(void)
{
	return buddy_alloc_pages(1);
}

void buddy_free_pages(uint64_t physical_address, uint64_t count)
{
	if (buddy_meta == 0 || count == 0)
	{
		return;
	}

	uint64_t order = 0;
	while (((uint64_t)1 << order) < count)
	{
		order++;
		if (order > BUDDY_MAX_ORDER)
		{
			kernel_panic_value("buddy: free block too large", count);
			return;
		}
	}

	buddy_free_block(physical_address / PAGE_SIZE, order);
}

void buddy_free_page(uint64_t physical_address)
{
	buddy_free_pages(physical_address, 1);
}

uint64_t buddy_get_free_pages(void)
{
	return buddy_free_count;
}

void buddy_lock_range(uint64_t physical_address, uint64_t size)
{
	if (buddy_meta == 0 || size == 0)
	{
		return;
	}

	uint64_t start = physical_address / PAGE_SIZE;
	uint64_t end = (physical_address + size + PAGE_SIZE - 1) / PAGE_SIZE;

	if (end > buddy_total_pages)
	{
		end = buddy_total_pages;
	}
	if (start >= end)
	{
		return;
	}

	uint64_t page = start;

	while (page < end)
	{
		if (buddy_meta[page] & META_USED)
		{
			page++;
			continue;
		}

		uint64_t order = buddy_containing_order(page);
		if (order == META_NO_BLOCK)
		{
			buddy_meta[page] = META_USED;
			page++;
			continue;
		}

		uint64_t block_pages = (uint64_t)1 << order;
		uint64_t block_start = page & ~(block_pages - 1);
		uint64_t block_end = block_start + block_pages;

		buddy_list_remove(order, buddy_page_pointer(block_start));
		buddy_free_count -= block_pages;

		for (uint64_t p = block_start; p < block_start + block_pages; p++)
		{
			buddy_meta[p] = META_USED;
		}

		uint64_t overlap_end = (block_end < end) ? block_end : end;

		buddy_push_range(block_start, page);
		buddy_push_range(overlap_end, block_end);

		page = overlap_end;
	}
}

void buddy_free_range(uint64_t physical_address, uint64_t size)
{
	if (buddy_meta == 0 || size == 0)
	{
		return;
	}

	buddy_push_range(physical_address / PAGE_SIZE,
		(physical_address + size + PAGE_SIZE - 1) / PAGE_SIZE);
}

void buddy_mark_slab(uint64_t physical_address, uint64_t order)
{
	if (buddy_meta == 0)
	{
		return;
	}

	uint64_t page = physical_address / PAGE_SIZE;
	if (page >= buddy_total_pages || (buddy_meta[page] & META_USED) == 0)
	{
		return;
	}

	buddy_meta[page] = (uint8_t)(META_USED | META_SLAB | (order & META_ORDER_MASK));
}

int buddy_page_is_slab(uint64_t physical_address)
{
	if (buddy_meta == 0)
	{
		return 0;
	}

	uint64_t page = physical_address / PAGE_SIZE;
	if (page >= buddy_total_pages)
	{
		return 0;
	}

	return (buddy_meta[page] & (META_USED | META_SLAB)) == (META_USED | META_SLAB);
}

uint64_t buddy_page_order(uint64_t physical_address)
{
	if (buddy_meta == 0)
	{
		return 0;
	}

	uint64_t page = physical_address / PAGE_SIZE;
	if (page >= buddy_total_pages)
	{
		return 0;
	}

	return (uint64_t)(buddy_meta[page] & META_ORDER_MASK);
}

void buddy_print_stats(void)
{
	serial_field("buddy free pages", buddy_free_count);

	for (uint64_t order = 0; order <= BUDDY_MAX_ORDER; order++)
	{
		uint64_t count = 0;

		for (buddy_block_t *block = buddy_free_lists[order]; block != 0; block = block->next)
		{
			count++;
		}

		if (count != 0)
		{
			serial_field("	free blocks of order", order);
			serial_field("	pages", count << order);
		}
	}
}
