#include "heap.h"
#include "memory.h"
#include "buddy.h"
#include "slab.h"
#include "panic.h"
#include "serial.h"

#define HEAP_MAGIC_INUSE 0x4845415055534544ULL
#define HEAP_MAGIC_FREE  0x4845415046524545ULL

#define HEAP_ALIGNMENT   16ULL
#define HEAP_HEADER_SIZE 16ULL
#define HEAP_MIN_BLOCK   32ULL

#define HEAP_INITIAL_SIZE (1ULL * 1024 * 1024)
#define HEAP_MAX_SIZE     (64ULL * 1024 * 1024)

#define ALIGN_UP(value, alignment) \
	(((value) + ((alignment) - 1)) & ~((uint64_t)(alignment) - 1))

typedef struct block_header
{
	uint64_t size;
	uint64_t magic;
} block_header_t;

typedef struct free_block
{
	block_header_t header;
	struct free_block *prev;
	struct free_block *next;
} free_block_t;

static uint64_t heap_base = 0;
static uint64_t heap_end = 0;
static uint64_t heap_limit = 0;

static uint64_t heap_total = 0;
static uint64_t heap_used = 0;

static free_block_t *free_list_head = 0;

static void free_list_remove(free_block_t *block)
{
	if (block->prev != 0)
	{
		block->prev->next = block->next;
	}
	else
	{
		free_list_head = block->next;
	}

	if (block->next != 0)
	{
		block->next->prev = block->prev;
	}

	block->prev = 0;
	block->next = 0;
}

static void free_list_insert(free_block_t *block)
{
	free_block_t *previous = 0;
	free_block_t *current = free_list_head;

	while (current != 0 && (uint64_t)current < (uint64_t)block)
	{
		previous = current;
		current = current->next;
	}

	block->prev = previous;
	block->next = current;

	if (previous != 0)
	{
		previous->next = block;
	}
	else
	{
		free_list_head = block;
	}

	if (current != 0)
	{
		current->prev = block;
	}
}

static void heap_release(free_block_t *block)
{
	block->header.magic = HEAP_MAGIC_FREE;

	free_block_t *next = (free_block_t *)((uint8_t *)block + block->header.size);
	if ((uint64_t)next >= heap_base && (uint64_t)next < heap_end &&
		next->header.magic == HEAP_MAGIC_FREE)
	{
		block->header.size += next->header.size;
		free_list_remove(next);
	}

	free_block_t *previous = 0;
	free_block_t *current = free_list_head;

	while (current != 0 && (uint64_t)current < (uint64_t)block)
	{
		previous = current;
		current = current->next;
	}

	if (previous != 0 && (uint8_t *)previous + previous->header.size == (uint8_t *)block)
	{
		previous->header.size += block->header.size;

		if (current != 0 &&
			(uint8_t *)previous + previous->header.size == (uint8_t *)current)
		{
			previous->header.size += current->header.size;
			free_list_remove(current);
		}

		return;
	}

	if (current != 0 && (uint8_t *)block + block->header.size == (uint8_t *)current)
	{
		block->header.size += current->header.size;
		free_list_remove(current);
	}

	free_list_insert(block);
}

static free_block_t *heap_find_fit(uint64_t need)
{
	for (free_block_t *block = free_list_head; block != 0; block = block->next)
	{
		if (block->header.size >= need)
		{
			return block;
		}
	}

	return 0;
}

static void heap_take(free_block_t *block, uint64_t need)
{
	if (block->header.size >= need + HEAP_MIN_BLOCK)
	{
		free_block_t *tail = (free_block_t *)((uint8_t *)block + need);
		tail->header.size = block->header.size - need;
		tail->header.magic = HEAP_MAGIC_FREE;

		tail->prev = block->prev;
		tail->next = block->next;

		if (tail->prev != 0)
		{
			tail->prev->next = tail;
		}
		else
		{
			free_list_head = tail;
		}

		if (tail->next != 0)
		{
			tail->next->prev = tail;
		}

		block->header.size = need;
	}
	else
	{
		free_list_remove(block);
	}

	block->header.magic = HEAP_MAGIC_INUSE;
	block->prev = 0;
	block->next = 0;
}

static int heap_grow(uint64_t need)
{
	if (heap_end >= heap_limit)
	{
		return 0;
	}

	uint64_t chunk = heap_end - heap_base;
	if (chunk < need)
	{
		chunk = need;
	}

	chunk = ALIGN_UP(chunk, PAGE_SIZE);

	if (heap_end + chunk > heap_limit)
	{
		chunk = heap_limit - heap_end;
	}

	if (chunk == 0)
	{
		return 0;
	}

	if (virtual_memory_manager_map_anonymous(heap_end, chunk, PAGE_WRITABLE) == 0)
	{
		return 0;
	}

	free_block_t *block = (free_block_t *)heap_end;
	block->header.size = chunk;

	heap_end += chunk;
	heap_total += chunk;

	heap_release(block);

	return 1;
}

void heap_init(const boot_info_t *info)
{
	uint64_t max_ram = physical_memory_manager_get_max_ram(info);
	if (max_ram == 0)
	{
		kernel_panic("heap_init: no usable memory reported");
	}

	heap_base = KERNEL_PHYSICAL_MAP_BASE + ALIGN_UP(max_ram, 2 * 1024 * 1024);
	heap_limit = heap_base + HEAP_MAX_SIZE;

	if (virtual_memory_manager_map_anonymous(heap_base, HEAP_INITIAL_SIZE, PAGE_WRITABLE) == 0)
	{
		kernel_panic("heap_init: cannot map the initial heap");
	}

	heap_end = heap_base + HEAP_INITIAL_SIZE;
	heap_total = HEAP_INITIAL_SIZE;

	free_block_t *first = (free_block_t *)heap_base;
	first->header.size = HEAP_INITIAL_SIZE;
	first->header.magic = HEAP_MAGIC_FREE;
	first->prev = 0;
	first->next = 0;

	free_list_head = first;

	serial_field("heap base", heap_base);
	serial_field("heap initial size", heap_total);
}

static void *kmalloc_pages(uint64_t need)
{
	uint64_t count = (need + PAGE_SIZE - 1) / PAGE_SIZE;
	uint64_t physical = physical_memory_manager_alloc_pages(count);

	if (physical == 0)
	{
		return 0;
	}

	block_header_t *header = (block_header_t *)physical_to_virtual(physical);
	header->size = count * PAGE_SIZE;
	header->magic = HEAP_MAGIC_INUSE;

	heap_used += header->size;

	return (uint8_t *)header + HEAP_HEADER_SIZE;
}

void *kmalloc(uint64_t size)
{
	if (size == 0)
	{
		return 0;
	}

	if (size <= SLAB_MAX_OBJECT)
	{
		void *object = slab_alloc(size);

		if (object != 0)
		{
			return object;
		}
	}

	uint64_t need = ALIGN_UP(size, HEAP_ALIGNMENT) + HEAP_HEADER_SIZE;
	if (need < HEAP_MIN_BLOCK)
	{
		need = HEAP_MIN_BLOCK;
	}

	if (need >= PAGE_SIZE)
	{
		return kmalloc_pages(need);
	}

	for (int attempt = 0; attempt < 2; attempt++)
	{
		free_block_t *block = heap_find_fit(need);

		if (block != 0)
		{
			heap_take(block, need);
			heap_used += block->header.size;

			return (uint8_t *)block + HEAP_HEADER_SIZE;
		}

		if (heap_grow(need) == 0)
		{
			break;
		}
	}

	return 0;
}

void *kcalloc(uint64_t count, uint64_t size)
{
	if (count != 0 && size > (~0ULL) / count)
	{
		return 0;
	}

	uint64_t total = count * size;

	void *pointer = kmalloc(total);
	if (pointer == 0)
	{
		return 0;
	}

	uint8_t *bytes = (uint8_t *)pointer;
	for (uint64_t i = 0; i < total; i++)
	{
		bytes[i] = 0;
	}

	return pointer;
}

static uint64_t allocator_usable_size(void *pointer)
{
	uint64_t address = (uint64_t)pointer;

	if (address >= heap_base && address < heap_end)
	{
		block_header_t *header = (block_header_t *)((uint8_t *)pointer - HEAP_HEADER_SIZE);

		if (header->magic != HEAP_MAGIC_INUSE)
		{
			kernel_panic_value("allocator: bad heap pointer", address);
		}

		return header->size - HEAP_HEADER_SIZE;
	}

	if (address < KERNEL_PHYSICAL_MAP_BASE)
	{
		kernel_panic_value("allocator: bad pointer", address);
	}

	if (buddy_page_is_slab(address - KERNEL_PHYSICAL_MAP_BASE))
	{
		uint64_t usable = slab_usable_size(pointer);

		if (usable == 0)
		{
			kernel_panic_value("allocator: bad slab pointer", address);
		}

		return usable;
	}

	block_header_t *header = (block_header_t *)((uint8_t *)pointer - HEAP_HEADER_SIZE);

	if (header->magic != HEAP_MAGIC_INUSE)
	{
		kernel_panic_value("allocator: bad pointer", address);
	}

	return header->size - HEAP_HEADER_SIZE;
}

void *krealloc(void *pointer, uint64_t size)
{
	if (pointer == 0)
	{
		return kmalloc(size);
	}

	if (size == 0)
	{
		kfree(pointer);
		return 0;
	}

	uint64_t usable = allocator_usable_size(pointer);
	if (size <= usable)
	{
		return pointer;
	}

	void *fresh = kmalloc(size);
	if (fresh == 0)
	{
		return 0;
	}

	uint8_t *from = (uint8_t *)pointer;
	uint8_t *to = (uint8_t *)fresh;
	for (uint64_t i = 0; i < usable; i++)
	{
		to[i] = from[i];
	}

	kfree(pointer);

	return fresh;
}

void kfree(void *pointer)
{
	if (pointer == 0)
	{
		return;
	}

	uint64_t address = (uint64_t)pointer;

	if (address >= heap_base && address < heap_end)
	{
		block_header_t *header = (block_header_t *)((uint8_t *)pointer - HEAP_HEADER_SIZE);

		if (header->magic == HEAP_MAGIC_FREE)
		{
			kernel_panic_value("kfree: double free", address);
		}

		if (header->magic != HEAP_MAGIC_INUSE)
		{
			kernel_panic_value("kfree: bad pointer", address);
		}

		heap_used -= header->size;
		heap_release((free_block_t *)header);
		return;
	}

	if (address < KERNEL_PHYSICAL_MAP_BASE)
	{
		kernel_panic_value("kfree: bad pointer", address);
	}

	if (buddy_page_is_slab(address - KERNEL_PHYSICAL_MAP_BASE))
	{
		slab_free(pointer);
		return;
	}

	block_header_t *header = (block_header_t *)((uint8_t *)pointer - HEAP_HEADER_SIZE);

	if (header->magic == HEAP_MAGIC_FREE)
	{
		kernel_panic_value("kfree: double free", address);
	}

	if (header->magic != HEAP_MAGIC_INUSE)
	{
		kernel_panic_value("kfree: bad pointer", address);
	}

	heap_used -= header->size;

	uint64_t size = header->size;
	uint64_t physical = (uint64_t)((uint8_t *)header - (uint8_t *)KERNEL_PHYSICAL_MAP_BASE);

	header->magic = 0;
	physical_memory_manager_free_pages(physical, size / PAGE_SIZE);
}

void heap_print_stats(void)
{
	uint64_t largest = 0;

	for (free_block_t *block = free_list_head; block != 0; block = block->next)
	{
		if (block->header.size > largest)
		{
			largest = block->header.size;
		}
	}

	serial_field("heap reserved", heap_total);
	serial_field("heap in use", heap_used);
	serial_field("heap free", heap_total - heap_used);
	serial_field("largest block", largest);
	slab_print_stats();
	serial_field("free frames", physical_memory_manager_get_free_pages());
}

static void allocator_test_expect(int condition, const char *message, uint64_t value)
{
	if (!condition)
	{
		kernel_panic_value(message, value);
	}
}

void allocator_self_test(void)
{
	enum { TEST_OBJECTS = SLAB_CLASS_COUNT * 4 };

	serial_write("Allocator self test...\r\n");

	uint64_t baseline = physical_memory_manager_get_free_pages();

	uint64_t pages[4];

	for (int i = 0; i < 4; i++)
	{
		pages[i] = physical_memory_manager_alloc_page();
		allocator_test_expect(pages[i] != 0, "self test: page alloc failed", i);
		allocator_test_expect((pages[i] & (PAGE_SIZE - 1)) == 0,
			"self test: unaligned page", pages[i]);
	}

	for (int i = 0; i < 4; i++)
	{
		physical_memory_manager_free_page(pages[i]);
	}

	allocator_test_expect(physical_memory_manager_get_free_pages() == baseline, "self test: pages did not coalesce", physical_memory_manager_get_free_pages());

	uint64_t block = physical_memory_manager_alloc_pages(1024);
	allocator_test_expect(block != 0, "self test: large block alloc failed", 0);
	allocator_test_expect((block & ((1024ULL * PAGE_SIZE) - 1)) == 0, "self test: block not aligned", block);

	physical_memory_manager_free_pages(block, 1024);
	allocator_test_expect(physical_memory_manager_get_free_pages() == baseline, "self test: large block did not merge", physical_memory_manager_get_free_pages());

	uint64_t victim = physical_memory_manager_alloc_pages(64);
	allocator_test_expect(victim != 0, "self test: cut block alloc failed", 0);
	physical_memory_manager_free_pages(victim, 64);

	physical_memory_manager_lock_region(victim + 10 * PAGE_SIZE, 2 * PAGE_SIZE);
	allocator_test_expect(physical_memory_manager_get_free_pages() == baseline - 2, "self test: lock did not cut the block", physical_memory_manager_get_free_pages());

	physical_memory_manager_lock_region(victim + 10 * PAGE_SIZE, 2 * PAGE_SIZE);
	allocator_test_expect(physical_memory_manager_get_free_pages() == baseline - 2, "self test: lock is not idempotent", physical_memory_manager_get_free_pages());

	physical_memory_manager_free_region(victim + 10 * PAGE_SIZE, 2 * PAGE_SIZE);
	allocator_test_expect(physical_memory_manager_get_free_pages() == baseline, "self test: unlock did not heal", physical_memory_manager_get_free_pages());

	physical_memory_manager_free_region(victim, 64 * PAGE_SIZE);
	allocator_test_expect(physical_memory_manager_get_free_pages() == baseline, "self test: free range is not idempotent", physical_memory_manager_get_free_pages());

	uint64_t again = physical_memory_manager_alloc_pages(64);
	allocator_test_expect(again != 0, "self test: merged block alloc failed", 0);
	physical_memory_manager_free_pages(again, 64);
	allocator_test_expect(physical_memory_manager_get_free_pages() == baseline, "self test: merged block did not come back", physical_memory_manager_get_free_pages());

	void *objects[TEST_OBJECTS];

	for (int i = 0; i < TEST_OBJECTS; i++)
	{
		uint64_t size = (uint64_t)16 << (i / 4);
		objects[i] = kmalloc(size);
		allocator_test_expect(objects[i] != 0, "self test: slab alloc failed", size);

		uint8_t *bytes = (uint8_t *)objects[i];
		for (uint64_t b = 0; b < size; b++)
		{
			bytes[b] = (uint8_t)(i + 1);
		}
	}

	for (int i = 0; i < TEST_OBJECTS; i++)
	{
		uint64_t size = (uint64_t)16 << (i / 4);
		uint8_t *bytes = (uint8_t *)objects[i];

		for (uint64_t b = 0; b < size; b++)
		{
			allocator_test_expect(bytes[b] == (uint8_t)(i + 1),
				"self test: slab object corrupted", i);
		}
	}

	for (int i = 0; i < TEST_OBJECTS; i++)
	{
		kfree(objects[i]);
	}

	allocator_test_expect(physical_memory_manager_get_free_pages() == baseline, "self test: slabs were not returned", physical_memory_manager_get_free_pages());

	void *middle = kmalloc(2000);
	allocator_test_expect(middle != 0, "self test: heap alloc failed", 0);

	void *spill = kmalloc(9000);
	allocator_test_expect(spill != 0, "self test: page backed alloc failed", 0);

	((uint8_t *)middle)[0] = 0x5A;
	((uint8_t *)spill)[0] = 0xA5;
	allocator_test_expect(((uint8_t *)middle)[0] == 0x5A, "self test: heap write lost", 0);
	allocator_test_expect(((uint8_t *)spill)[0] == 0xA5, "self test: page write lost", 0);

	kfree(middle);
	kfree(spill);

	allocator_test_expect(physical_memory_manager_get_free_pages() == baseline, "self test: heap and pages did not come back", physical_memory_manager_get_free_pages());

	serial_write("Allocator self test passed!\r\n");
}
