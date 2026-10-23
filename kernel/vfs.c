#include "vfs.h"
#include "heap.h"
#include "serial.h"
#include "spinlock.h"

static fs_driver_t* drivers[VFS_MAX_DRIVERS];
static uint32_t driver_count = 0;

static vfs_mount_t mounts[VFS_MAX_MOUNTS];
static uint32_t mount_count = 0;

static vfs_node_t vfs_root;
static vfs_node_t* root_override = NULL;
static spinlock_t vfs_lock = {0};

// TODO: Remove when libc will done
static int vfs_streq(const char* a, const char* b)
{
	while (*a != '\0' && *a == *b)
	{
		a++;
		b++;
	}

	return (*a == '\0' && *b == '\0');
}

static uint32_t vfs_strcpy(char* dst, uint32_t dst_size, const char* src)
{
	uint32_t len = 0;

	while (src[len] != '\0' && len + 1 < dst_size)
	{
		dst[len] = src[len];
		len++;
	}
	dst[len] = '\0';

	return len;
}

static vfs_node_t* node_canonical(vfs_node_t* node)
{
	return (node->canonical != NULL) ? node->canonical : node;
}

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
		node_canonical(node)->refs++;
	}
}

void vfs_node_put(vfs_node_t* node)
{
	if (node == NULL)
	{
		return;
	}

	node = node_canonical(node);

	if (node->refs == 0)
	{
		return;
	}

	if (--node->refs == 0 && node->operations != NULL && node->operations->release != NULL)
	{
		node->operations->release(node);
	}
}

static vfs_node_t* mount_find(const char* name)
{
	for (uint32_t i = 0; i < mount_count; i++)
	{
		if (vfs_streq(mounts[i].name, name))
		{
			return mounts[i].node;
		}
	}

	return NULL;
}

static uint32_t mount_add(const char* name, vfs_node_t* node)
{
	if (name == NULL || node == NULL || mount_count >= VFS_MAX_MOUNTS || mount_find(name) != NULL)
	{
		return 0xFFFFFFFF;
	}

	vfs_mount_t* slot = &mounts[mount_count];
	vfs_strcpy(slot->name, sizeof(slot->name), name);

	uint32_t len = 0;
	while (slot->name[len] != '\0')
	{
		len++;
	}

	if (len > 1 && slot->name[0] == '/')
	{
		for (uint32_t j = 0; j < len; j++)
		{
			slot->name[j] = slot->name[j + 1];
		}
	}

	slot->node = node;
	node->canonical = node;

	return mount_count++;
}

int vfs_mount(const char* name, block_device_t* dev)
{
	if (name == NULL || name[0] == '\0')
	{
		return -1;
	}

	spin_lock(&vfs_lock);

	if (mount_find(name) != NULL)
	{
		spin_unlock(&vfs_lock);
		return -1;
	}

	for (uint32_t i = 0; i < driver_count; i++)
	{
		vfs_node_t* node = drivers[i]->mount(dev);
		if (node == NULL)
		{
			continue;
		}

		int result = (mount_add(name, node) == 0xFFFFFFFF) ? -1 : 0;
		spin_unlock(&vfs_lock);
		return result;
	}

	spin_unlock(&vfs_lock);
	return -1;
}

int vfs_mount_root(block_device_t* dev)
{
	if (dev == NULL)
	{
		return -1;
	}

	spin_lock(&vfs_lock);

	if (root_override != NULL)
	{
		spin_unlock(&vfs_lock);
		return -1;
	}

	for (uint32_t i = 0; i < driver_count; i++)
	{
		vfs_node_t* node = drivers[i]->mount(dev);
		if (node == NULL)
		{
			continue;
		}

		root_override = node;
		spin_unlock(&vfs_lock);

		serial_write("VFS: mounted ");
		serial_write(dev->name);
		serial_write(" as /\r\n");

		return 0;
	}

	spin_unlock(&vfs_lock);
	return -1;
}

static vfs_node_t* vfs_walk(vfs_node_t* start, const char* path, vfs_node_t** out_parent, char* out_name, int* out_tail)
{
	vfs_node_t* current = start;
	vfs_node_t* parent = NULL;
	char component[VFS_NAMELENGTH];
	char last[VFS_NAMELENGTH];

	last[0] = '\0';

	int state = 0;

	uint32_t i = 0;
	if (path[0] == '/')
	{
		i++;
	}

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
			state = 2;
			break;
		}

		parent = current;
		vfs_strcpy(last, sizeof(last), component);

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
			state = 1;

			for (uint32_t probe = i; path[probe] != '\0'; probe++)
			{
				if (path[probe] != '/')
				{
					state = 2;
					break;
				}
			}

			break;
		}

		current = next;
	}

	if (out_parent != NULL) *out_parent = (state == 2) ? NULL : parent;
	if (out_name != NULL) vfs_strcpy(out_name, VFS_NAMELENGTH, last);
	if (out_tail != NULL) *out_tail = (state == 1) ? 1 : 0;

	return (state == 0) ? current : NULL;
}

static vfs_node_t* vfs_create_tail(const char* path, int directory)
{
	vfs_node_t* parent = NULL;
	char name[VFS_NAMELENGTH];
	int tail_ok = 0;

	vfs_walk(&vfs_root, path, &parent, name, &tail_ok);

	if (!tail_ok || parent == NULL || name[0] == '\0' || parent->operations == NULL)
	{
		return NULL;
	}

	if (directory)
	{
		if (parent->operations->mkdir == NULL ||
			parent->operations->mkdir(parent, name, 0777) != 0)
		{
			return NULL;
		}
	}
	else if (parent->operations->create == NULL ||
			 parent->operations->create(parent, name, 0777) != 0)
	{
		return NULL;
	}

	return parent->operations->finddir(parent, name);
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
		node = vfs_create_tail(path, 0);
	}

	if (node != NULL)
	{
		vfs_node_get(node);
	}

	spin_unlock(&vfs_lock);

	return node;
}

int vfs_exists(const char* path)
{
	if (path == NULL || path[0] == '\0')
	{
		return 0;
	}

	spin_lock(&vfs_lock);

	vfs_node_t* node = vfs_walk(&vfs_root, path, NULL, NULL, NULL);

	spin_unlock(&vfs_lock);

	return (node != NULL) ? 1 : 0;
}

int vfs_mkdir(const char* path)
{
	if (path == NULL || path[0] == '\0')
	{
		return -1;
	}

	spin_lock(&vfs_lock);

	int result = (vfs_create_tail(path, 1) != NULL) ? 0 : -1;

	spin_unlock(&vfs_lock);

	return result;
}

int vfs_unlink(const char* path)
{
	if (path == NULL || path[0] == '\0')
	{
		return -1;
	}

	spin_lock(&vfs_lock);

	vfs_node_t* parent = NULL;
	char name[VFS_NAMELENGTH];
	int missing = 0;

	vfs_walk(&vfs_root, path, &parent, name, &missing);

	int result = -1;

	if (!missing && parent != NULL && name[0] != '\0' && parent->operations != NULL &&
		parent->operations->unlink != NULL)
	{
		result = parent->operations->unlink(parent, name);
	}

	spin_unlock(&vfs_lock);

	return result;
}

int vfs_rmdir(const char* path)
{
	if (path == NULL || path[0] == '\0')
	{
		return -1;
	}

	spin_lock(&vfs_lock);

	vfs_node_t* parent = NULL;
	char name[VFS_NAMELENGTH];
	int missing = 0;

	vfs_walk(&vfs_root, path, &parent, name, &missing);

	int result = -1;

	if (!missing && parent != NULL && name[0] != '\0' && parent->operations != NULL &&
		parent->operations->rmdir != NULL)
	{
		result = parent->operations->rmdir(parent, name);
	}

	spin_unlock(&vfs_lock);

	return result;
}

void vfs_close(vfs_node_t* node)
{
	vfs_node_put(node);
}

int vfs_read(vfs_node_t* node, uint64_t offset, uint32_t size, void* buffer)
{
	return vfs_node_read(node, offset, size, buffer);
}

int vfs_write(vfs_node_t* node, uint64_t offset, uint32_t size, const void* buffer)
{
	return vfs_node_write(node, offset, size, buffer);
}

int vfs_list_directory(const char* path, vfs_dir_visitor_t visitor, void* context)
{
	if (path == NULL || visitor == NULL)
	{
		return -1;
	}

	vfs_node_t* dir = vfs_open(path, VFS_RDONLY);
	if (dir == NULL)
	{
		return -1;
	}

	if (dir->type != VFS_DIRECTORY || dir->operations == NULL ||
		dir->operations->readdir == NULL)
	{
		vfs_node_put(dir);
		return -1;
	}

	vfs_node_t entry;
	int result = 0;
	uint32_t index = 0;

	while (dir->operations->readdir(dir, index, &entry) == 0)
	{
		result = visitor(&entry, context);
		if (result != 0)
		{
			break;
		}
		index++;
	}

	vfs_node_put(dir);

	return result;
}

static int vfs_count_visitor(const vfs_node_t* entry, void* context)
{
	(void)entry;

	(*(uint32_t*)context)++;

	return 0;
}

uint32_t vfs_count_directory(const char* path)
{
	uint32_t count = 0;

	if (vfs_list_directory(path, vfs_count_visitor, &count) != 0)
	{
		return 0;
	}

	return count;
}

int64_t vfs_read_file(const char* path, void* buffer, uint32_t size)
{
	if (path == NULL || buffer == NULL)
	{
		return -1;
	}

	vfs_node_t* node = vfs_open(path, VFS_RDONLY);
	if (node == NULL)
	{
		return -1;
	}

	if (node->type != VFS_FILE && node->type != VFS_BLOCKDEVICE)
	{
		vfs_node_put(node);
		return -1;
	}

	if ((uint64_t)size > node->size)
	{
		size = (uint32_t)node->size;
	}

	int moved = vfs_node_read(node, 0, size, buffer);

	vfs_node_put(node);

	return moved;
}

int64_t vfs_write_file(const char* path, const void* buffer, uint32_t size, uint32_t flags)
{
	if (path == NULL || buffer == NULL)
	{
		return -1;
	}

	vfs_node_t* node = vfs_open(path, flags | VFS_WRONLY);
	if (node == NULL)
	{
		return -1;
	}

	if (node->type != VFS_FILE && node->type != VFS_BLOCKDEVICE)
	{
		vfs_node_put(node);
		return -1;
	}

	int moved = vfs_node_write(node, 0, size, buffer);

	vfs_node_put(node);

	return moved;
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

	if (root_override != NULL && root_override->operations != NULL &&
		root_override->operations->finddir != NULL)
	{
		return root_override->operations->finddir(root_override, name);
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

	vfs_strcpy(out_node->name, sizeof(out_node->name), mounts[index].name);

	out_node->type = src->type;
	out_node->size = src->size;
	out_node->inode_num = src->inode_num;
	out_node->operations = src->operations;
	out_node->private_data = src->private_data;
	out_node->canonical = node_canonical(src);
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

	index -= mount_count;

	if (root_override != NULL && root_override->operations != NULL &&
		root_override->operations->readdir != NULL)
	{
		return root_override->operations->readdir(root_override, index, out_node);
	}

	if (ramfs_backing != NULL)
	{
		return ramfs_backing->operations->readdir(ramfs_backing, index, out_node);
	}

	return -1;
}

static int root_create(vfs_node_t* node, const char* name, uint16_t mask)
{
	(void)node;

	if (root_override != NULL && root_override->operations != NULL &&
		root_override->operations->create != NULL)
	{
		return root_override->operations->create(root_override, name, mask);
	}

	if (ramfs_backing != NULL)
	{
		return ramfs_backing->operations->create(ramfs_backing, name, mask);
	}

	return -1;
}

static int root_mkdir(vfs_node_t* node, const char* name, uint16_t mask)
{
	(void)node;

	if (root_override != NULL && root_override->operations != NULL &&
		root_override->operations->mkdir != NULL)
	{
		return root_override->operations->mkdir(root_override, name, mask);
	}

	if (ramfs_backing != NULL)
	{
		return ramfs_backing->operations->mkdir(ramfs_backing, name, mask);
	}

	return -1;
}

static int root_unlink(vfs_node_t* node, const char* name)
{
	(void)node;

	if (root_override != NULL && root_override->operations != NULL &&
		root_override->operations->unlink != NULL)
	{
		return root_override->operations->unlink(root_override, name);
	}

	if (ramfs_backing != NULL)
	{
		return ramfs_backing->operations->unlink(ramfs_backing, name);
	}

	return -1;
}

static int root_rmdir(vfs_node_t* node, const char* name)
{
	(void)node;

	if (mount_find(name) != NULL)
	{
		return -1;
	}

	if (root_override != NULL && root_override->operations != NULL &&
		root_override->operations->rmdir != NULL)
	{
		return root_override->operations->rmdir(root_override, name);
	}

	if (ramfs_backing != NULL)
	{
		return ramfs_backing->operations->rmdir(ramfs_backing, name);
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
	root_mkdir,
	root_unlink,
	root_rmdir,
	NULL
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
	vfs_root.canonical = &vfs_root;

	ramfs_backing = ramfs_make_root();
	if (ramfs_backing == NULL)
	{
		serial_write("VFS: ramfs root allocation failed\r\n");
		return;
	}

	if (mount_add("devices", devfs_make_root()) == 0xFFFFFFFF)
	{
		serial_write("VFS: /devices mount failed\r\n");
		return;
	}

	serial_write("VFS inited!\r\n");
}
