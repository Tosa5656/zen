#include "vfs.h"
#include "heap.h"
#include "serial.h"
#include "spinlock.h"

static fs_driver_t* drivers[VFS_MAX_DRIVERS];
static uint32_t driver_count = 0;

static vfs_mount_t mounts[VFS_MAX_MOUNTS];
static uint32_t mount_count = 0;

static vfs_node_t vfs_root;
static spinlock_t vfs_lock = {0};

void fs_register(fs_driver_t* driver)
{
	if (driver == NULL || driver->mount == NULL || driver_count >= VFS_MAX_DRIVERS)
	{
		return;
	}

	drivers[driver_count++] = driver;
}

void vfs_node_get(vfs_node_t* node)
{
	if (node != NULL)
	{
		node->refs++;
	}
}

void vfs_node_put(vfs_node_t* node)
{
	if (node == NULL || node->refs == 0)
	{
		return;
	}

	node->refs--;
}

static vfs_node_t* mount_find(const char* name)
{
	for (uint32_t i = 0; i < mount_count; i++)
	{
		const char* m = mounts[i].name;

		uint32_t j = 0;
		while (m[j] != '\0' && name[j] == m[j])
		{
			j++;
		}

		if (name[j] == '\0' && m[j] == '\0')
		{
			vfs_node_get(mounts[i].node);
			return mounts[i].node;
		}
	}

	return NULL;
}

static uint32_t mount_add(const char* name, vfs_node_t* node)
{
	if (name == NULL || node == NULL || mount_count >= VFS_MAX_MOUNTS)
	{
		return 0xFFFFFFFF;
	}

	vfs_mount_t* slot = &mounts[mount_count];
	uint32_t len = 0;

	while (len + 1 < sizeof(slot->name) && name[len] != '\0')
	{
		slot->name[len] = name[len];
		len++;
	}
	slot->name[len] = '\0';

	if (len > 1 && slot->name[0] == '/')
	{
		for (uint32_t j = 0; j < len; j++)
		{
			slot->name[j] = slot->name[j + 1];
		}
	}

	slot->node = node;

	return mount_count++;
}

int vfs_mount(const char* name, block_device_t* dev)
{
	if (name == NULL || name[0] == '\0')
	{
		return -1;
	}

	for (uint32_t i = 0; i < driver_count; i++)
	{
		vfs_node_t* node = drivers[i]->mount(dev);

		if (mount_add(name, node) == 0xFFFFFFFF)
		{
			return -1;
		}

		return 0;
	}

	return -1;
}

static vfs_node_t* vfs_walk(vfs_node_t* start, const char* path, vfs_node_t** out_parent, char* out_name, int* out_tail)
{
	vfs_node_t* current = start;
	char component[VFS_NAMELENGTH];

	uint32_t i = 0;
	if (path[0] == '/')
	{
		i++;
	}

	int missing = 0;

	while (path[i] != '\0')
	{
		uint32_t len = 0;

		while (path[i] != '\0' && path[i] != '/' && len + 1 < sizeof(component))
		{
			component[len++] = path[i++];
		}
		while (path[i] == '/')
		{
			i++;
		}
		component[len] = '\0';

		if (component[0] == '\0')
		{
			continue;
		}

		if (current->type != VFS_DIRECTORY ||
			current->operations == NULL || current->operations->finddir == NULL)
		{
			if (out_parent != NULL) *out_parent = NULL;
			if (out_tail != NULL) *out_tail = 0;
			return NULL;
		}

		vfs_node_t* next = NULL;

		if (current == &vfs_root)
		{
			next = mount_find(component);
		}

		if (next == NULL)
		{
			next = current->operations->finddir(current, component);
		}

		if (next == NULL)
		{
			uint32_t probe = i;
			int tail_ok = 1;

			while (path[probe] != '\0')
			{
				if (path[probe] != '/')
				{
					tail_ok = 0;
					break;
				}
				probe++;
			}

			missing = 1;

			if (out_tail != NULL) *out_tail = tail_ok;

			if (out_parent != NULL)
			{
				*out_parent = tail_ok ? current : NULL;
			}
			if (out_name != NULL && tail_ok)
			{
				uint32_t c = 0;
				while (component[c] != '\0')
				{
					out_name[c] = component[c];
					c++;
				}
				out_name[c] = '\0';
			}

			return NULL;
		}

		current = next;
	}

	if (missing == 0)
	{
		return current;
	}

	if (out_parent != NULL) *out_parent = NULL;
	if (out_tail != NULL) *out_tail = 0;

	return NULL;
}

vfs_node_t* vfs_open(const char* path, uint32_t flags)
{
	if (path == NULL || path[0] == '\0')
	{
		return NULL;
	}

	spin_lock(&vfs_lock);

	vfs_node_t* node = vfs_walk(&vfs_root, path, NULL, NULL, NULL);

	if (node == NULL && (flags & VFS_CREATE))
	{
		vfs_node_t* parent = NULL;
		char name[VFS_NAMELENGTH];
		int tail_ok = 0;

		vfs_walk(&vfs_root, path, &parent, name, &tail_ok);

		if (tail_ok && parent != NULL && name[0] != '\0' &&
			parent->operations != NULL && parent->operations->create != NULL &&
			parent->operations->create(parent, name, 0777) == 0)
		{
			node = parent->operations->finddir(parent, name);
		}
	}

	if (node != NULL)
	{
		node->refs++;
	}

	spin_unlock(&vfs_lock);

	return node;
}

int vfs_node_read(vfs_node_t* node, uint64_t offset, uint32_t size, void* buffer)
{
	if (node == NULL || node->operations == NULL || node->operations->read == NULL ||
		node->type == VFS_DIRECTORY || node->type == VFS_SYMLINK)
	{
		return -1;
	}

	return node->operations->read(node, offset, size, buffer);
}

int vfs_node_write(vfs_node_t* node, uint64_t offset, uint32_t size, const void* buffer)
{
	if (node == NULL || node->operations == NULL || node->operations->write == NULL ||
		node->type == VFS_DIRECTORY || node->type == VFS_SYMLINK)
	{
		return -1;
	}

	return node->operations->write(node, offset, size, buffer);
}

vfs_node_t* ramfs_make_root(void);
vfs_node_t* devfs_make_root(void);

static vfs_node_t* ramfs_backing = NULL;

static vfs_node_t* root_finddir(vfs_node_t* node, const char* name)
{
	(void)node;

	vfs_node_t* mounted = mount_find(name);
	if (mounted != NULL)
	{
		return mounted;
	}

	if (ramfs_backing != NULL)
	{
		return ramfs_backing->operations->finddir(ramfs_backing, name);
	}

	return NULL;
}

static int root_fill_from_mount(uint32_t index, vfs_node_t* out_node)
{
	vfs_node_t* src = mounts[index].node;
	const char* mname = mounts[index].name;

	uint32_t len = 0;
	while (mname[len] != '\0' && len + 1 < sizeof(out_node->name))
	{
		out_node->name[len] = mname[len];
		len++;
	}
	out_node->name[len] = '\0';

	out_node->type = src->type;
	out_node->size = src->size;
	out_node->inode_num = src->inode_num;
	out_node->operations = src->operations;
	out_node->private_data = src->private_data;
	out_node->refs = 0;

	return 0;
}

static int root_readdir(vfs_node_t* node, uint32_t index, vfs_node_t* out_node)
{
	(void)node;

	if (index < mount_count)
	{
		return root_fill_from_mount(index, out_node);
	}

	if (ramfs_backing != NULL)
	{
		return ramfs_backing->operations->readdir(ramfs_backing, index - mount_count, out_node);
	}

	return -1;
}

static int root_create(vfs_node_t* node, const char* name, uint16_t mask)
{
	(void)node;

	if (ramfs_backing != NULL)
	{
		return ramfs_backing->operations->create(ramfs_backing, name, mask);
	}

	return -1;
}

static int root_mkdir(vfs_node_t* node, const char* name, uint16_t mask)
{
	(void)node;

	if (ramfs_backing != NULL)
	{
		return ramfs_backing->operations->mkdir(ramfs_backing, name, mask);
	}

	return -1;
}

static vfs_operations_t root_operations =
{
	NULL,
	NULL,
	root_finddir,
	root_readdir,
	root_create,
	root_mkdir
};

void vfs_init(void)
{
	vfs_root.name[0] = '/';
	vfs_root.name[1] = '\0';
	vfs_root.type = VFS_DIRECTORY;
	vfs_root.size = 0;
	vfs_root.inode_num = 0;
	vfs_root.refs = 1;
	vfs_root.operations = &root_operations;
	vfs_root.private_data = NULL;

	ramfs_backing = ramfs_make_root();
	if (ramfs_backing == NULL)
	{
		serial_write("VFS: ramfs root allocation failed\r\n");
		return;
	}

	if (mount_add("dev", devfs_make_root()) == 0xFFFFFFFF)
	{
		serial_write("VFS: /dev mount failed\r\n");
		return;
	}

	serial_write("VFS inited!\r\n");
}
