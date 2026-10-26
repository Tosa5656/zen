#include "vfs.h"
#include "block_device.h"
#include "keyboard.h"
#include "framebuffer.h"
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

#define DEV_CONSOLE_BUFFER 4096

static char console_buffer[DEV_CONSOLE_BUFFER];
static uint64_t console_head = 0;
static uint64_t console_tail = 0;

static const char scancode_ascii[128] =
{
	0, 0, '1', '2', '3', '4', '5', '6', '7', '8', '9', '0', '-', '=', '\b',
	'\t', 'q', 'w', 'e', 'r', 't', 'y', 'u', 'i', 'o', 'p', '[', ']', '\n',
	0, 'a', 's', 'd', 'f', 'g', 'h', 'j', 'k', 'l', ';', '\'', '`',
	0, '\\', 'z', 'x', 'c', 'v', 'b', 'n', 'm', ',', '.', '/', 0,
	'*', 0, ' ', 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	'7', '8', '9', '-', '4', '5', '6', '+', '1', '2', '3', '0', '.'
};

static int keyboard_shift = 0;

static int keyboard_getchar(void)
{
	uint8_t scancode;

	if (!keyboard_pop(&scancode))
	{
		return -1;
	}

	if (scancode == 0x2A || scancode == 0x36)
	{
		keyboard_shift = 1;
		return -1;
	}
	if (scancode == 0xAA || scancode == 0xB6)
	{
		keyboard_shift = 0;
		return -1;
	}

	if (scancode & 0x80)
	{
		return -1;
	}

	int c = scancode_ascii[scancode];
	if (c == 0)
	{
		return -1;
	}

	if (keyboard_shift)
	{
		if (c >= 'a' && c <= 'z')
		{
			c -= 32;
		}
		else if (c >= '0' && c <= '9')
		{
			const char shifted[] = ")!@#$%^&*(";
			c = shifted[c - '0'];
		}
	}

	return c;
}

static void console_push(char c)
{
	uint64_t flags = irq_save();

	uint64_t next = (console_head + 1) % DEV_CONSOLE_BUFFER;
	if (next != console_tail)
	{
		console_buffer[console_head] = c;
		console_head = next;
	}

	irq_restore(flags);
}

uint64_t dev_console_read(char* out, uint64_t count)
{
	uint64_t flags = irq_save();
	uint64_t got = 0;

	while (got < count && console_tail != console_head)
	{
		out[got++] = console_buffer[console_tail];
		console_tail = (console_tail + 1) % DEV_CONSOLE_BUFFER;
	}

	irq_restore(flags);

	return got;
}

uint64_t dev_console_available(void)
{
	uint64_t flags = irq_save();
	uint64_t count = (console_head + DEV_CONSOLE_BUFFER - console_tail) % DEV_CONSOLE_BUFFER;
	irq_restore(flags);

	return count;
}

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

	while (got < size)
	{
		int c = keyboard_getchar();
		if (c < 0)
		{
			break;
		}
		out[got++] = (uint8_t)c;
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
		console_push(text[i]);
	}

	framebuffer_console_write(text, size);

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

static vfs_node_t* devfs_block_node(uint32_t index)
{
	block_device_t* dev = block_device_get(index);
	if (dev == NULL)
	{
		return NULL;
	}

	vfs_node_t* node = &block_nodes[index];

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
		node->inode_num = index + 1;
		node->refs = 0;
		node->operations = &blockdev_operations;
		node->private_data = dev;
		node->canonical = node;
	}

	return node;
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
			return devfs_block_node(i);
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
		out_node->canonical = &stdio_nodes[i];
		out_node->refs = 0;

		return 0;
	}

	vfs_node_t* block = devfs_block_node(index - 3);
	if (block == NULL)
	{
		return -1;
	}

	uint32_t len = 0;
	while (block->name[len] != '\0' && len + 1 < sizeof(out_node->name))
	{
		out_node->name[len] = block->name[len];
		len++;
	}
	out_node->name[len] = '\0';
	out_node->type = block->type;
	out_node->size = block->size;
	out_node->inode_num = block->inode_num;
	out_node->operations = block->operations;
	out_node->private_data = block->private_data;
	out_node->canonical = block;
	out_node->refs = 0;

	return 0;
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

static int devfs_unlink(vfs_node_t* node, const char* name)
{
	(void)node;
	(void)name;

	return -1;
}

static int devfs_rmdir(vfs_node_t* node, const char* name)
{
	(void)node;
	(void)name;

	return -1;
}

vfs_operations_t chardev_operations =
{
	chardev_read,
	chardev_write,
	NULL,
	NULL,
	NULL,
	NULL,
	NULL,
	NULL,
	NULL
};

#define BLOCKDEV_SECTOR 512

static int blockdev_transfer(vfs_node_t* node, uint64_t offset, uint32_t size,
							 void* buffer, int writing)
{
	block_device_t* dev = (block_device_t*)node->private_data;

	if (dev == NULL)
	{
		return -1;
	}

	uint64_t total = dev->total_sectors * BLOCKDEV_SECTOR;

	if (offset >= total || size > total - offset)
	{
		return -1;
	}

	if (size == 0)
	{
		return 0;
	}

	uint8_t* out = (uint8_t*)buffer;
	uint64_t done = 0;

	while (done < size)
	{
		uint64_t absolute = offset + done;
		uint64_t lba = absolute / BLOCKDEV_SECTOR;
		uint32_t in_sector = (uint32_t)(absolute % BLOCKDEV_SECTOR);
		uint32_t chunk = BLOCKDEV_SECTOR - in_sector;

		if (chunk > size - done)
		{
			chunk = (uint32_t)(size - done);
		}

		if (in_sector == 0 && chunk == BLOCKDEV_SECTOR)
		{
			uint32_t count = (uint32_t)((size - done) / BLOCKDEV_SECTOR);
			if (count == 0)
			{
				count = 1;
			}

			int rc = writing
				? (dev->write_blocks ? dev->write_blocks(dev, lba, count, out + done) : -1)
				: (dev->read_blocks ? dev->read_blocks(dev, lba, count, out + done) : -1);

			if (rc != 0)
			{
				return -1;
			}

			done += (uint64_t)count * BLOCKDEV_SECTOR;
			continue;
		}

		uint8_t sector[BLOCKDEV_SECTOR];

		if (writing)
		{
			if (in_sector != 0 || chunk != BLOCKDEV_SECTOR)
			{
				if (dev->read_blocks == NULL || dev->read_blocks(dev, lba, 1, sector) != 0)
				{
					return -1;
				}
			}

			for (uint32_t i = 0; i < chunk; i++)
			{
				sector[in_sector + i] = out[done + i];
			}

			if (dev->write_blocks == NULL || dev->write_blocks(dev, lba, 1, sector) != 0)
			{
				return -1;
			}
		}
		else
		{
			if (dev->read_blocks == NULL || dev->read_blocks(dev, lba, 1, sector) != 0)
			{
				return -1;
			}

			for (uint32_t i = 0; i < chunk; i++)
			{
				out[done + i] = sector[in_sector + i];
			}
		}

		done += chunk;
	}

	return (int)size;
}

static int blockdev_read(vfs_node_t* node, uint64_t offset, uint32_t size, void* buffer)
{
	return blockdev_transfer(node, offset, size, buffer, 0);
}

static int blockdev_write(vfs_node_t* node, uint64_t offset, uint32_t size, const void* buffer)
{
	return blockdev_transfer(node, offset, size, (void*)buffer, 1);
}

vfs_operations_t blockdev_operations =
{
	blockdev_read,
	blockdev_write,
	NULL,
	NULL,
	NULL,
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
		stdio_nodes[i].canonical = &stdio_nodes[i];
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
	devfs_root_node.canonical = &devfs_root_node;

	static vfs_operations_t devfs_root_operations =
	{
		NULL,
		NULL,
		devfs_finddir,
		devfs_readdir,
		devfs_create,
		devfs_mkdir,
		devfs_unlink,
		devfs_rmdir,
		NULL
	};

	devfs_root_node.operations = &devfs_root_operations;

	return &devfs_root_node;
}
