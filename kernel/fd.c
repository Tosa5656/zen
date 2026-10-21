#include "fd.h"
#include "heap.h"
#include "scheduler.h"
#include "serial.h"

fd_table_t* fd_table_create_default(void)
{
	fd_table_t* table = kcalloc(1, sizeof(fd_table_t));
	if (table == NULL)
	{
		return NULL;
	}

	static const struct
	{
		const char* path;
		uint32_t flags;
	} std[3] =
	{
		{ "dev/stdin",  VFS_RDONLY },
		{ "dev/stdout", VFS_WRONLY },
		{ "dev/stderr", VFS_WRONLY }
	};

	for (int fd = 0; fd < 3; fd++)
	{
		vfs_node_t* node = vfs_open(std[fd].path, VFS_RDONLY);
		vfs_fd_t* handle = kcalloc(1, sizeof(vfs_fd_t));

		if (node == NULL || handle == NULL)
		{
			if (node != NULL)
			{
				node->refs--;
			}
			kfree(handle);
			fd_table_destroy(table);
			return NULL;
		}

		handle->node = node;
		handle->offset = 0;
		handle->flags = std[fd].flags;
		handle->refs = 1;

		table->descriptors[fd] = handle;
	}

	return table;
}

void fd_table_destroy(fd_table_t* table)
{
	if (table == NULL)
	{
		return;
	}

	spin_lock(&table->lock);

	for (int fd = 0; fd < FD_MAX; fd++)
	{
		vfs_fd_t* handle = table->descriptors[fd];
		if (handle == NULL)
		{
			continue;
		}

		table->descriptors[fd] = NULL;

		if (--handle->refs == 0)
		{
			if (handle->node != NULL)
			{
				handle->node->refs--;
			}
			kfree(handle);
		}
	}

	spin_unlock(&table->lock);
	kfree(table);
}

fd_table_t* fd_table_current(void)
{
	thread_t* thread = current_thread;

	if (thread != NULL && thread->fds != NULL)
	{
		return (fd_table_t*)thread->fds;
	}

	if (thread != NULL)
	{
		thread->fds = fd_table_create_default();
		if ((fd_table_t*)thread->fds != NULL)
		{
			return (fd_table_t*)thread->fds;
		}
	}

	static fd_table_t* kernel_table = NULL;
	if (kernel_table == NULL)
	{
		kernel_table = fd_table_create_default();
	}

	return kernel_table;
}

vfs_fd_t* fd_get(int fd)
{
	if (fd < 0 || fd >= FD_MAX)
	{
		return NULL;
	}

	fd_table_t* table = fd_table_current();
	if (table == NULL)
	{
		return NULL;
	}

	return table->descriptors[fd];
}

static int fd_store(int fd, vfs_node_t* node, uint32_t flags)
{
	if (fd < 0 || fd >= FD_MAX)
	{
		return -1;
	}

	fd_table_t* table = fd_table_current();
	if (table == NULL)
	{
		return -1;
	}

	vfs_fd_t* handle = kcalloc(1, sizeof(vfs_fd_t));
	if (handle == NULL)
	{
		return -1;
	}

	spin_lock(&table->lock);

	int slot = -1;
	for (int i = (fd >= 0 && fd < FD_MAX) ? fd : 0; i < FD_MAX; i++)
	{
		if (table->descriptors[i] == NULL && (i > FD_STDERR || fd == i))
		{
			slot = i;
			break;
		}
	}

	if (slot == -1)
	{
		spin_unlock(&table->lock);
		kfree(handle);
		return -1;
	}

	handle->node = node;
	handle->flags = flags;
	handle->offset = 0;
	handle->refs = 1;
	node->refs++;

	table->descriptors[slot] = handle;

	spin_unlock(&table->lock);

	return slot;
}

int fd_open(const char* path, uint32_t flags)
{
	if ((flags & VFS_RDWR) == 0 && path != NULL)
	{
		flags |= VFS_RDONLY;
	}

	vfs_node_t* node = vfs_open(path, flags);
	if (node == NULL)
	{
		return -1;
	}

	if (node->type == VFS_DIRECTORY)
	{
		node->refs--;
		return -1;
	}

	return fd_store(FD_STDERR + 1, node, flags);
}

uint64_t fd_read(int fd, void* buffer, uint64_t count)
{
	vfs_fd_t* handle = fd_get(fd);
	if (handle == NULL || handle->node == NULL || buffer == NULL)
	{
		return (uint64_t)-1;
	}

	if (count > 0x7FFFFFFF)
	{
		count = 0x7FFFFFFF;
	}

	int moved = vfs_node_read(handle->node, handle->offset, (uint32_t)count, buffer);
	if (moved < 0)
	{
		return (uint64_t)-1;
	}

	if (handle->node->type == VFS_FILE || handle->node->type == VFS_BLOCKDEVICE)
	{
		handle->offset += moved;
	}

	return (uint64_t)moved;
}

uint64_t fd_write(int fd, const void* buffer, uint64_t count)
{
	vfs_fd_t* handle = fd_get(fd);
	if (handle == NULL || handle->node == NULL || buffer == NULL)
	{
		return (uint64_t)-1;
	}

	if ((handle->flags & (VFS_WRONLY | VFS_RDWR)) == 0)
	{
		return (uint64_t)-1;
	}

	if (count > 0x7FFFFFFF)
	{
		count = 0x7FFFFFFF;
	}

	int moved = vfs_node_write(handle->node, handle->offset, (uint32_t)count, buffer);
	if (moved < 0)
	{
		return (uint64_t)-1;
	}

	handle->offset += moved;

	return (uint64_t)moved;
}

int fd_close(int fd)
{
	if (fd < 0 || fd >= FD_MAX || fd <= FD_STDERR)
	{
		return -1;
	}

	fd_table_t* table = fd_table_current();
	if (table == NULL)
	{
		return -1;
	}

	spin_lock(&table->lock);

	vfs_fd_t* handle = table->descriptors[fd];
	if (handle == NULL)
	{
		spin_unlock(&table->lock);
		return -1;
	}

	table->descriptors[fd] = NULL;

	if (--handle->refs > 0)
	{
		spin_unlock(&table->lock);
		return 0;
	}

	if (handle->node != NULL)
	{
		handle->node->refs--;
	}

	spin_unlock(&table->lock);
	kfree(handle);

	return 0;
}

int fd_dup(int fd)
{
	vfs_fd_t* handle = fd_get(fd);
	if (handle == NULL)
	{
		return -1;
	}

	fd_table_t* table = fd_table_current();
	if (table == NULL)
	{
		return -1;
	}

	spin_lock(&table->lock);

	int slot = -1;
	for (int i = FD_STDERR + 1; i < FD_MAX; i++)
	{
		if (table->descriptors[i] == NULL)
		{
			slot = i;
			break;
		}
	}

	if (slot == -1)
	{
		spin_unlock(&table->lock);
		return -1;
	}

	handle->refs++;
	table->descriptors[slot] = handle;

	spin_unlock(&table->lock);

	return slot;
}
