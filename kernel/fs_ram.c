#include "vfs.h"
#include "heap.h"
#include "spinlock.h"
#include "serial.h"

#define RAMFS_NAME_MAX 64

vfs_operations_t ramfs_operations;

static int ramfs_streq(const char* a, const char* b)
{
	while (*a != '\0' && *a == *b)
	{
		a++;
		b++;
	}

	return (*a == '\0' && *b == '\0');
}

typedef struct ram_dire
{
	char name[RAMFS_NAME_MAX];
	vfs_type_t type;
	uint8_t* data;
	uint64_t size;
	uint64_t capacity;
	struct ram_dire* children;
	struct ram_dire* next;
	vfs_node_t* node;
} ram_dire_t;

static spinlock_t ramfs_lock = {0};
static uint32_t ramfs_next_inode = 1;

static vfs_node_t* ramfs_node_build(ram_dire_t* entry)
{
	vfs_node_t* node = kcalloc(1, sizeof(vfs_node_t));
	if (node == NULL)
	{
		return NULL;
	}

	uint32_t len = 0;
	while (entry->name[len] != '\0' && len + 1 < sizeof(node->name))
	{
		node->name[len] = entry->name[len];
		len++;
	}
	node->name[len] = '\0';

	node->type = entry->type;
	node->size = entry->size;
	node->inode_num = ramfs_next_inode++;
	node->refs = 0;
	node->operations = &ramfs_operations;
	node->private_data = entry;

	entry->node = node;

	return node;
}

static ram_dire_t* ramfs_child_lookup(ram_dire_t* dir, const char* name)
{
	for (ram_dire_t* child = dir->children; child != NULL; child = child->next)
	{
		if (ramfs_streq(child->name, name))
		{
			return child;
		}
	}

	return NULL;
}

static ram_dire_t* ramfs_entry_from(vfs_node_t* node)
{
	ram_dire_t* entry = (ram_dire_t*)node->private_data;

	if (entry == NULL || entry->node != node)
	{
		return NULL;
	}

	return entry;
}

static void ramfs_sync_size(vfs_node_t* node, ram_dire_t* entry)
{
	entry->size = node->size;
}

static vfs_node_t* ramfs_finddir(vfs_node_t* node, const char* name)
{
	ram_dire_t* dir = ramfs_entry_from(node);
	if (dir == NULL || dir->type != VFS_DIRECTORY)
	{
		return NULL;
	}

	spin_lock(&ramfs_lock);

	ram_dire_t* child = ramfs_child_lookup(dir, name);

	spin_unlock(&ramfs_lock);

	if (child == NULL)
	{
		return NULL;
	}

	if (child->node == NULL)
	{
		child->node = ramfs_node_build(child);
	}

	return child->node;
}

static int ramfs_readdir(vfs_node_t* node, uint32_t index, vfs_node_t* out_node)
{
	ram_dire_t* dir = ramfs_entry_from(node);
	if (dir == NULL || dir->type != VFS_DIRECTORY)
	{
		return -1;
	}

	spin_lock(&ramfs_lock);

	ram_dire_t* child = dir->children;
	for (uint32_t i = 0; i != index && child != NULL; i++)
	{
		child = child->next;
	}

	if (child != NULL && child->node == NULL)
	{
		child->node = ramfs_node_build(child);
	}

	if (child == NULL)
	{
		spin_unlock(&ramfs_lock);
		return -1;
	}

	uint32_t len = 0;
	while (child->name[len] != '\0' && len + 1 < sizeof(out_node->name))
	{
		out_node->name[len] = child->name[len];
		len++;
	}
	out_node->name[len] = '\0';
	out_node->type = child->type;
	out_node->size = child->size;
	out_node->inode_num = (child->node != NULL) ? child->node->inode_num : 0;
	out_node->operations = node->operations;
	out_node->private_data = child;
	out_node->refs = 0;

	spin_unlock(&ramfs_lock);

	return 0;
}

static ram_dire_t* ramfs_entry_new(const char* name, vfs_type_t type)
{
	ram_dire_t* entry = kcalloc(1, sizeof(ram_dire_t));
	if (entry == NULL)
	{
		return NULL;
	}

	uint32_t len = 0;
	while (name[len] != '\0' && len + 1 < RAMFS_NAME_MAX)
	{
		entry->name[len] = name[len];
		len++;
	}
	entry->name[len] = '\0';

	entry->type = type;

	return entry;
}

static int ramfs_create(vfs_node_t* node, const char* name, uint16_t mask)
{
	(void)mask;

	ram_dire_t* dir = ramfs_entry_from(node);
	if (dir == NULL || dir->type != VFS_DIRECTORY)
	{
		return -1;
	}

	spin_lock(&ramfs_lock);

	if (ramfs_child_lookup(dir, name) != NULL)
	{
		spin_unlock(&ramfs_lock);
		return -1;
	}

	ram_dire_t* child = ramfs_entry_new(name, VFS_FILE);
	if (child == NULL)
	{
		spin_unlock(&ramfs_lock);
		return -1;
	}

	child->next = dir->children;
	dir->children = child;

	spin_unlock(&ramfs_lock);

	return 0;
}

static int ramfs_mkdir(vfs_node_t* node, const char* name, uint16_t mask)
{
	ram_dire_t* dir = ramfs_entry_from(node);
	if (dir == NULL || dir->type != VFS_DIRECTORY)
	{
		return -1;
	}

	(void)mask;

	spin_lock(&ramfs_lock);

	if (ramfs_child_lookup(dir, name) != NULL)
	{
		spin_unlock(&ramfs_lock);
		return -1;
	}

	ram_dire_t* child = ramfs_entry_new(name, VFS_DIRECTORY);
	if (child == NULL)
	{
		spin_unlock(&ramfs_lock);
		return -1;
	}

	child->next = dir->children;
	dir->children = child;

	spin_unlock(&ramfs_lock);

	return 0;
}

static int ramfs_read(vfs_node_t* node, uint64_t offset, uint32_t size, void* buffer)
{
	ram_dire_t* entry = ramfs_entry_from(node);
	if (entry == NULL || entry->type != VFS_FILE || entry->data == NULL)
	{
		return (entry != NULL && entry->type == VFS_FILE && size == 0) ? 0 : -1;
	}

	if (offset >= entry->size)
	{
		return 0;
	}

	uint64_t available = entry->size - offset;
	uint32_t moved = (size < available) ? size : (uint32_t)available;

	for (uint32_t i = 0; i < moved; i++)
	{
		((uint8_t*)buffer)[i] = entry->data[offset + i];
	}

	return (int)moved;
}

static int ramfs_write(vfs_node_t* node, uint64_t offset, uint32_t size, const void* buffer)
{
	ram_dire_t* entry = ramfs_entry_from(node);
	if (entry == NULL || entry->type != VFS_FILE)
	{
		return -1;
	}

	spin_lock(&ramfs_lock);

	uint64_t needed = offset + size;
	if (needed > entry->capacity)
	{
		uint64_t grown = (entry->capacity == 0) ? 4096 : entry->capacity;
		while (grown < needed)
		{
			grown *= 2;
		}

		uint8_t* fresh = krealloc(entry->data, grown);
		if (fresh == NULL)
		{
			spin_unlock(&ramfs_lock);
			return -1;
		}

		entry->data = fresh;
		entry->capacity = grown;
	}

	for (uint32_t i = 0; i < size; i++)
	{
		entry->data[offset + i] = ((const uint8_t*)buffer)[i];
	}

	if (needed > entry->size)
	{
		entry->size = needed;
	}
	node->size = entry->size;
	ramfs_sync_size(node, entry);

	spin_unlock(&ramfs_lock);

	return (int)size;
}

vfs_operations_t ramfs_operations =
{
	ramfs_read,
	ramfs_write,
	ramfs_finddir,
	ramfs_readdir,
	ramfs_create,
	ramfs_mkdir
};

vfs_node_t* ramfs_make_root(void)
{
	ram_dire_t* entry = kcalloc(1, sizeof(ram_dire_t));
	if (entry == NULL)
	{
		return NULL;
	}

	entry->name[0] = '/';
	entry->type = VFS_DIRECTORY;

	vfs_node_t* node = ramfs_node_build(entry);
	if (node == NULL)
	{
		kfree(entry);
		return NULL;
	}

	node->operations = &ramfs_operations;
	node->name[0] = '/';
	node->name[1] = '\0';

	return node;
}
