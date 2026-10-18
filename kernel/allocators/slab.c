#include "slab.h"
#include "buddy.h"
#include "memory.h"
#include "panic.h"
#include "serial.h"

#define SLAB_MAGIC            0x534C41424D414743ULL
#define SLAB_OBJECT_FREE_MAGIC 0xD01EFEEDD01EFEEDULL

typedef struct slab_object_link
{
	struct slab_object_link *next;
	uint64_t free_magic;
} slab_object_link_t;

typedef struct slab slab_t;

typedef struct slab_cache
{
	const char *name;
	uint64_t object_size;
	uint64_t header_size;
	slab_t *partial;
	slab_t *full;
	uint64_t slab_count;
	uint64_t live_objects;
} slab_cache_t;

struct slab
{
	uint64_t magic;
	slab_cache_t *cache;
	uint32_t in_use;
	uint32_t object_count;
	slab_object_link_t *free_objects;
	slab_t *next;
	slab_t *prev;
};

static const uint64_t slab_class_sizes[SLAB_CLASS_COUNT] =
{
	16, 32, 64, 128, 256, 512, 1024
};

static slab_cache_t slab_caches[SLAB_CLASS_COUNT];

static void slab_list_push(slab_t **head, slab_t *slab)
{
	slab->prev = 0;
	slab->next = *head;

	if (slab->next != 0)
	{
		slab->next->prev = slab;
	}

	*head = slab;
}

static void slab_list_remove(slab_t **head, slab_t *slab)
{
	if (slab->prev != 0)
	{
		slab->prev->next = slab->next;
	}
	else
	{
		*head = slab->next;
	}

	if (slab->next != 0)
	{
		slab->next->prev = slab->prev;
	}

	slab->prev = 0;
	slab->next = 0;
}

static int slab_class_for(uint64_t size)
{
	for (int i = 0; i < SLAB_CLASS_COUNT; i++)
	{
		if (size <= slab_class_sizes[i])
		{
			return i;
		}
	}

	return -1;
}

static uint64_t slab_header_size(void)
{
	uint64_t size = sizeof(slab_t);
	return (size + 15) & ~15ULL;
}

static slab_t *slab_from_pointer(const void *pointer)
{
	uint64_t physical =
		(uint64_t)((const uint8_t *)pointer - (uint8_t *)KERNEL_PHYSICAL_MAP_BASE);

	uint64_t order = buddy_page_order(physical);
	uint64_t base = physical & ~(((uint64_t)PAGE_SIZE << order) - 1);

	slab_t *slab = (slab_t *)physical_to_virtual(base);
	if (slab->magic != SLAB_MAGIC)
	{
		return 0;
	}

	return slab;
}

static slab_t *slab_grow(slab_cache_t *cache)
{
	uint64_t physical = buddy_alloc_page();
	if (physical == 0)
	{
		return 0;
	}

	buddy_mark_slab(physical, 0);

	slab_t *slab = (slab_t *)physical_to_virtual(physical);
	slab->magic = SLAB_MAGIC;
	slab->cache = cache;
	slab->in_use = 0;
	slab->free_objects = 0;
	slab->next = 0;
	slab->prev = 0;
	slab->object_count = (uint32_t)((PAGE_SIZE - cache->header_size) / cache->object_size);

	uint8_t *base = (uint8_t *)slab + cache->header_size;

	for (uint32_t i = slab->object_count; i-- > 0;)
	{
		slab_object_link_t *object =
			(slab_object_link_t *)(base + (uint64_t)i * cache->object_size);

		object->next = slab->free_objects;
		object->free_magic = SLAB_OBJECT_FREE_MAGIC;
		slab->free_objects = object;
	}

	cache->slab_count++;
	return slab;
}

void slab_init(void)
{
	for (int i = 0; i < SLAB_CLASS_COUNT; i++)
	{
		slab_caches[i].name = "slab";
		slab_caches[i].object_size = slab_class_sizes[i];
		slab_caches[i].header_size = slab_header_size();
		slab_caches[i].partial = 0;
		slab_caches[i].full = 0;
		slab_caches[i].slab_count = 0;
		slab_caches[i].live_objects = 0;
	}

	if (slab_header_size() % 16 != 0)
	{
		kernel_panic_value("slab: header is not 16 byte aligned", slab_header_size());
	}

	serial_field("slab header size", slab_header_size());
}

void *slab_alloc(uint64_t size)
{
	int class_index = slab_class_for(size);
	if (class_index < 0)
	{
		return 0;
	}

	slab_cache_t *cache = &slab_caches[class_index];

	slab_t *slab = cache->partial;
	if (slab == 0)
	{
		slab = slab_grow(cache);
		if (slab == 0)
		{
			return 0;
		}

		slab_list_push(&cache->partial, slab);
	}

	slab_object_link_t *object = slab->free_objects;
	slab->free_objects = object->next;
	object->free_magic = 0;

	slab->in_use++;
	cache->live_objects++;

	if (slab->free_objects == 0)
	{
		slab_list_remove(&cache->partial, slab);
		slab_list_push(&cache->full, slab);
	}

	return object;
}

int slab_owns(const void *pointer)
{
	if ((const uint8_t *)pointer < (const uint8_t *)KERNEL_PHYSICAL_MAP_BASE)
	{
		return 0;
	}

	return slab_from_pointer(pointer) != 0;
}

void slab_free(void *pointer)
{
	slab_t *slab = slab_from_pointer(pointer);
	if (slab == 0)
	{
		kernel_panic_value("slab_free: not a slab object", (uint64_t)pointer);
	}

	slab_cache_t *cache = slab->cache;
	slab_object_link_t *object = (slab_object_link_t *)pointer;

	if (slab->in_use == 0)
	{
		kernel_panic_value("slab_free: empty slab", (uint64_t)pointer);
	}

	if (object->free_magic == SLAB_OBJECT_FREE_MAGIC)
	{
		kernel_panic_value("slab_free: double free", (uint64_t)pointer);
	}

	int was_full = (slab->free_objects == 0);

	object->next = slab->free_objects;
	object->free_magic = SLAB_OBJECT_FREE_MAGIC;
	slab->free_objects = object;
	slab->in_use--;
	cache->live_objects--;

	if (was_full)
	{
		slab_list_remove(&cache->full, slab);
		slab_list_push(&cache->partial, slab);
	}

	if (slab->in_use == 0)
	{
		slab_list_remove(&cache->partial, slab);
		cache->slab_count--;

		uint64_t physical =
			(uint64_t)((uint8_t *)slab - (uint8_t *)KERNEL_PHYSICAL_MAP_BASE);

		slab->magic = 0;
		buddy_free_page(physical);
	}
}

uint64_t slab_usable_size(const void *pointer)
{
	slab_t *slab = slab_from_pointer(pointer);
	if (slab == 0)
	{
		return 0;
	}

	return slab->cache->object_size;
}

void slab_print_stats(void)
{
	uint64_t live = 0;
	uint64_t reserved = 0;

	for (int i = 0; i < SLAB_CLASS_COUNT; i++)
	{
		live += slab_caches[i].live_objects;
		reserved += slab_caches[i].slab_count;
	}

	serial_field("slab slabs", reserved);
	serial_field("slab objects", live);
}

void slab_print_details(void)
{
	for (int i = 0; i < SLAB_CLASS_COUNT; i++)
	{
		slab_cache_t *cache = &slab_caches[i];
		uint64_t capacity = cache->slab_count * ((PAGE_SIZE - cache->header_size) / cache->object_size);

		serial_field("  	size", cache->object_size);
		serial_field("  	objects", cache->live_objects);
		serial_field("  	capacity", capacity);
	}
}
