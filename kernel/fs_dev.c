#include "vfs.h"
#include "block_device.h"
#include "keyboard.h"
#include "serial.h"
#include "spinlock.h"

enum
{
	DEV_STDIN = 0,
	DEV_STDOUT,
	DEV_STDERR
};

static vfs_node_t devfs_root_node;
static vfs_node_t stdio_nodes[3];
static vfs_node_t block_nodes[MAX_BLOCK_DEVICES];

vfs_operations_t blockdev_operations;
vfs_operations_t chardev_operations;

static const char* const stdio_names[3] = { "stdin", "stdout", "stderr" };

static int chardev_read(vfs_node_t* node, uint64_t offset, uint32_t size, void* buffer)
{
	(void)offset;

	if (size == 0)
	{
		return 0;
	}

	if ((uint64_t)node->private_data != DEV_STDIN)
	{
		return -1;
	}

	uint64_t got = 0;
	uint8_t* out = (uint8_t*)buffer;

	while (got < size && keyboard_pop(&out[got]))
	{
		got++;
	}

	return (int)got;
}

static int chardev_write(vfs_node_t* node, uint64_t offset, uint32_t size, const void* buffer)
{
	(void)offset;

	if ((uint64_t)node->private_data == DEV_STDIN)
	{
		return -1;
	}

	const char* text = (const char*)buffer;

	for (uint32_t i = 0; i < size; i++)
	{
		serial_putc(text[i]);
	}

	return (int)size;
}

static vfs_node_t* devfs_lookup_stdio(const char* name)
{
	for (int i = 0; i < 3; i++)
	{
		uint32_t j = 0;
		while (stdio_names[i][j] != '\0' && name[j] == stdio_names[i][j])
		{
			j++;
		}

		if (name[j] == '\0' && stdio_names[i][j] == '\0')
		{
			return &stdio_nodes[i];
		}
	}

	return NULL;
}

static vfs_node_t* devfs_lookup_block(const char* name)
{
	for (uint32_t i = 0; i < block_device_get_count(); i++)
	{
		block_device_t* dev = block_device_get(i);
		if (dev == NULL)
		{
			continue;
		}

		uint32_t j = 0;
		while (dev->name[j] != '\0' && name[j] == dev->name[j])
		{
			j++;
		}

		if (name[j] == '\0' && dev->name[j] == '\0')
		{
			vfs_node_t* node = &block_nodes[i];

			if (node->type == 0)
			{
				uint32_t len = 0;
				while (dev->name[len] != '\0' && len + 1 < sizeof(node->name))
				{
					node->name[len] = dev->name[len];
					len++;
				}
				node->name[len] = '\0';

				node->type = VFS_BLOCKDEVICE;
				node->size = dev->total_sectors * 512;
				node->inode_num = i + 1;
				node->refs = 0;
				node->operations = &blockdev_operations;
				node->private_data = dev;
			}

			return node;
		}
	}

	return NULL;
}

static vfs_node_t* devfs_finddir(vfs_node_t* node, const char* name)
{
	(void)node;

	vfs_node_t* found = devfs_lookup_stdio(name);
	if (found == NULL)
	{
		found = devfs_lookup_block(name);
	}

	return found;
}

static int devfs_readdir(vfs_node_t* node, uint32_t index, vfs_node_t* out_node)
{
	(void)node;

	if (index < 3)
	{
		uint32_t i = index;

		uint32_t len = 0;
		while (stdio_names[i][len] != '\0' && len + 1 < sizeof(out_node->name))
		{
			out_node->name[len] = stdio_names[i][len];
			len++;
		}
		out_node->name[len] = '\0';
		out_node->type = VFS_CHARDEVICE;
		out_node->size = 0;
		out_node->inode_num = i + 1;
		out_node->operations = stdio_nodes[i].operations;
		out_node->private_data = stdio_nodes[i].private_data;
		out_node->refs = 0;

		return 0;
	}

	uint32_t blocks = block_device_get_count();
	if (index - 3 < blocks)
	{
		block_device_t* dev = block_device_get(index - 3);
		uint32_t len = 0;

		while (dev->name[len] != '\0' && len + 1 < sizeof(out_node->name))
		{
			out_node->name[len] = dev->name[len];
			len++;
		}
		out_node->name[len] = '\0';
		out_node->type = VFS_BLOCKDEVICE;
		out_node->size = dev->total_sectors * 512;
		out_node->inode_num = index + 1;
		out_node->operations = block_nodes[index - 3].operations;
		out_node->private_data = dev;
		out_node->refs = 0;

		return 0;
	}

	return -1;
}

static int devfs_create(vfs_node_t* node, const char* name, uint16_t mask)
{
	(void)node;
	(void)name;
	(void)mask;

	return -1;
}

static int devfs_mkdir(vfs_node_t* node, const char* name, uint16_t mask)
{
	(void)node;
	(void)name;
	(void)mask;

	return -1;
}

vfs_operations_t chardev_operations =
{
	chardev_read,
	chardev_write,
	NULL,
	NULL,
	NULL,
	NULL
};

static int blockdev_read(vfs_node_t* node, uint64_t offset, uint32_t size, void* buffer)
{
	block_device_t* dev = (block_device_t*)node->private_data;

	if (dev == NULL || (offset & 0x1FF) != 0 || (size & 0x1FF) != 0)
	{
		return -1;
	}

	uint64_t lba = offset / 512;
	uint64_t count = size / 512;

	if (count == 0)
	{
		return 0;
	}

	if (lba + count > dev->total_sectors)
	{
		return -1;
	}

	return (dev->read_blocks != NULL) ? dev->read_blocks(dev, lba, (uint32_t)count, buffer) : -1;
}

static int blockdev_write(vfs_node_t* node, uint64_t offset, uint32_t size, const void* buffer)
{
	block_device_t* dev = (block_device_t*)node->private_data;

	if (dev == NULL || (offset & 0x1FF) != 0 || (size & 0x1FF) != 0)
	{
		return -1;
	}

	uint64_t lba = offset / 512;
	uint64_t count = size / 512;

	if (count == 0)
	{
		return 0;
	}

	if (lba + count > dev->total_sectors)
	{
		return -1;
	}

	return (dev->write_blocks != NULL) ? dev->write_blocks(dev, lba, (uint32_t)count, (void*)buffer) : -1;
}

vfs_operations_t blockdev_operations =
{
	blockdev_read,
	blockdev_write,
	NULL,
	NULL,
	NULL,
	NULL
};

vfs_node_t* devfs_make_root(void)
{
	for (int i = 0; i < 3; i++)
	{
		uint32_t len = 0;
		while (stdio_names[i][len] != '\0' && len + 1 < sizeof(stdio_nodes[i].name))
		{
			stdio_nodes[i].name[len] = stdio_names[i][len];
			len++;
		}
		stdio_nodes[i].name[len] = '\0';

		stdio_nodes[i].type = VFS_CHARDEVICE;
		stdio_nodes[i].size = 0;
		stdio_nodes[i].inode_num = i + 1;
		stdio_nodes[i].refs = 0;
		stdio_nodes[i].operations = &chardev_operations;
		stdio_nodes[i].private_data = (void*)(uint64_t)i;
	}

	devfs_root_node.name[0] = 'd';
	devfs_root_node.name[1] = 'e';
	devfs_root_node.name[2] = 'v';
	devfs_root_node.name[3] = '\0';
	devfs_root_node.type = VFS_DIRECTORY;
	devfs_root_node.size = 0;
	devfs_root_node.inode_num = 0;
	devfs_root_node.refs = 1;
	devfs_root_node.operations = NULL;
	devfs_root_node.private_data = NULL;

	static vfs_operations_t devfs_root_operations =
	{
		NULL,
		NULL,
		devfs_finddir,
		devfs_readdir,
		devfs_create,
		devfs_mkdir
	};

	devfs_root_node.operations = &devfs_root_operations;

	return &devfs_root_node;
}
