#include "fd.h"
#include "heap.h"
#include "scheduler.h"
#include "serial.h"
#include "spinlock.h"

static spinlock_t fd_current_lock = {0};
static fd_table_t* kernel_table = NULL;
static process_t* fd_wait_head = NULL;

static void handle_free(vfs_fd_t* handle)
{
	if (handle->node != NULL)
	{
		vfs_node_put(handle->node);
	}
	kfree(handle);
}

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
		{ "devices/stdin",  VFS_RDONLY },
		{ "devices/stdout", VFS_WRONLY },
		{ "devices/stderr", VFS_WRONLY }
	};

	for (int fd = 0; fd < 3; fd++)
	{
		vfs_node_t* node = vfs_open(std[fd].path, std[fd].flags);
		vfs_fd_t* handle = kcalloc(1, sizeof(vfs_fd_t));

		if (node == NULL || handle == NULL)
		{
			if (node != NULL)
			{
				vfs_node_put(node);
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

	vfs_fd_t* doomed[FD_MAX];
	uint32_t doomed_count = 0;

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
			doomed[doomed_count++] = handle;
		}
	}

	spin_unlock(&table->lock);

	for (uint32_t i = 0; i < doomed_count; i++)
	{
		handle_free(doomed[i]);
	}

	kfree(table);
}

fd_table_t* fd_table_current(void)
{
	process_t* process = current_process;

	if (process != NULL && process->fds != NULL)
	{
		return (fd_table_t*)process->fds;
	}

	spin_lock(&fd_current_lock);

	if (process != NULL && process->fds == NULL)
	{
		process->fds = fd_table_create_default();
	}

	fd_table_t* table = (process != NULL) ? (fd_table_t*)process->fds : NULL;

	if (table == NULL)
	{
		if (kernel_table == NULL)
		{
			kernel_table = fd_table_create_default();
		}
		table = kernel_table;
	}

	spin_unlock(&fd_current_lock);

	return table;
}

static vfs_fd_t* fd_pin(int fd, fd_table_t** out_table)
{
	if (out_table != NULL)
	{
		*out_table = NULL;
	}

	if (fd < 0 || fd >= FD_MAX)
	{
		return NULL;
	}

	fd_table_t* table = fd_table_current();
	if (table == NULL)
	{
		return NULL;
	}

	spin_lock(&table->lock);

	vfs_fd_t* handle = table->descriptors[fd];
	if (handle != NULL)
	{
		handle->refs++;
	}

	spin_unlock(&table->lock);

	if (out_table != NULL)
	{
		*out_table = table;
	}

	return handle;
}

static void fd_unpin(fd_table_t* table, vfs_fd_t* handle)
{
	if (handle == NULL)
	{
		return;
	}

	spin_lock(&table->lock);

	int last = (--handle->refs == 0);

	spin_unlock(&table->lock);

	if (last)
	{
		handle_free(handle);
	}
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

	handle->node = node;
	handle->flags = flags;
	handle->offset = 0;
	handle->refs = 1;

	spin_lock(&table->lock);

	int slot = -1;
	for (int i = fd; i < FD_MAX; i++)
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
		handle_free(handle);
		return -1;
	}

	table->descriptors[slot] = handle;

	spin_unlock(&table->lock);

	return slot;
}

int fd_open(const char* path, uint32_t flags)
{
	if ((flags & VFS_RDWR) == 0)
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
		vfs_node_put(node);
		return -1;
	}

	return fd_store(FD_STDERR + 1, node, flags);
}

uint64_t fd_read(int fd, void* buffer, uint64_t count)
{
	fd_table_t* table = NULL;
	vfs_fd_t* handle = fd_pin(fd, &table);

	if (handle == NULL || handle->node == NULL || buffer == NULL)
	{
		fd_unpin(table, handle);
		return (uint64_t)-1;
	}

	if ((handle->flags & VFS_RDONLY) == 0)
	{
		fd_unpin(table, handle);
		return (uint64_t)-1;
	}

	if (count > 0x7FFFFFFF)
	{
		count = 0x7FFFFFFF;
	}

	vfs_type_t type = handle->node->type;
	int moved = vfs_node_read(handle->node, handle->offset, (uint32_t)count, buffer);

	if (moved > 0 && (type == VFS_FILE || type == VFS_BLOCKDEVICE))
	{
		handle->offset += (uint64_t)moved;
	}

	fd_unpin(table, handle);

	if (moved < 0)
	{
		return (uint64_t)-1;
	}

	if (moved == 0 && count > 0 && type == VFS_CHARDEVICE)
	{
		return FD_WOULD_BLOCK;
	}

	return (uint64_t)moved;
}

uint64_t fd_write(int fd, const void* buffer, uint64_t count)
{
	fd_table_t* table = NULL;
	vfs_fd_t* handle = fd_pin(fd, &table);

	if (handle == NULL || handle->node == NULL || buffer == NULL)
	{
		fd_unpin(table, handle);
		return (uint64_t)-1;
	}

	if ((handle->flags & VFS_WRONLY) == 0)
	{
		fd_unpin(table, handle);
		return (uint64_t)-1;
	}

	if (count > 0x7FFFFFFF)
	{
		count = 0x7FFFFFFF;
	}

	int moved = vfs_node_write(handle->node, handle->offset, (uint32_t)count, buffer);

	if (moved > 0)
	{
		handle->offset += (uint64_t)moved;
	}

	fd_unpin(table, handle);

	return (moved < 0) ? (uint64_t)-1 : (uint64_t)moved;
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

	int last = (--handle->refs == 0);

	spin_unlock(&table->lock);

	if (last)
	{
		handle_free(handle);
	}

	return 0;
}

int fd_dup(int fd)
{
	if (fd < 0 || fd >= FD_MAX)
	{
		return -1;
	}

	return fd_dup_from(fd, FD_STDERR + 1);
}

int fd_dup_from(int fd, int start)
{
	if (fd < 0 || fd >= FD_MAX || start < 0 || start >= FD_MAX)
	{
		return -1;
	}

	if (start <= FD_STDERR)
	{
		start = FD_STDERR + 1;
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

	int slot = -1;
	for (int i = start; i < FD_MAX; i++)
	{
		if (table->descriptors[i] == NULL)
		{
			slot = i;
			break;
		}
	}

	if (slot != -1)
	{
		handle->refs++;
		table->descriptors[slot] = handle;
	}

	spin_unlock(&table->lock);

	return slot;
}

static int fd_get_flags(int fd)
{
	fd_table_t* table = NULL;
	vfs_fd_t* handle = fd_pin(fd, &table);

	if (handle == NULL || handle->node == NULL)
	{
		fd_unpin(table, handle);
		return -1;
	}

	uint32_t flags = handle->flags;

	fd_unpin(table, handle);

	return (int)flags;
}

int fd_fcntl(int fd, int cmd, uint64_t arg)
{
	switch (cmd)
	{
	case FD_FCNTL_DUPFD:
	case FD_FCNTL_DUPFD_CLOEXEC:
	{
		if (arg >= FD_MAX)
		{
			return -1;
		}

		return fd_dup_from(fd, (int)arg);
	}
	case FD_FCNTL_GETFD:
	{
		return (fd_get_flags(fd) == -1) ? -1 : 0;
	}
	case FD_FCNTL_SETFD:
	case FD_FCNTL_SETFL:
	{
		return (fd_get_flags(fd) == -1) ? -1 : 0;
	}
	case FD_FCNTL_GETFL:
	{
		int flags = fd_get_flags(fd);

		if (flags == -1)
		{
			return -1;
		}

		if ((flags & VFS_RDWR) == VFS_RDWR)
		{
			return FD_FCNTL_O_RDWR;
		}

		if ((flags & VFS_WRONLY) != 0)
		{
			return FD_FCNTL_O_WRONLY;
		}

		return FD_FCNTL_O_RDONLY;
	}
	default:
		return -1;
	}
}

int64_t fd_seek(int fd, int64_t offset, int whence)
{
	fd_table_t* table = NULL;
	vfs_fd_t* handle = fd_pin(fd, &table);

	if (handle == NULL || handle->node == NULL)
	{
		fd_unpin(table, handle);
		return -1;
	}

	int64_t result = -1;

	if (handle->node->type == VFS_FILE || handle->node->type == VFS_BLOCKDEVICE)
	{
		int64_t base = 0;

		if (whence == VFS_SEEK_CUR)
		{
			base = (int64_t)handle->offset;
		}
		else if (whence == VFS_SEEK_END)
		{
			base = (int64_t)handle->node->size;
		}
		else if (whence != VFS_SEEK_SET)
		{
			fd_unpin(table, handle);
			return -1;
		}

		int64_t target = base + offset;

		if (target >= 0 && (uint64_t)target <= handle->node->size)
		{
			handle->offset = (uint64_t)target;
			result = target;
		}
	}

	fd_unpin(table, handle);

	return result;
}

void fd_wait_add(process_t* process)
{
	if (process == NULL)
	{
		return;
	}

	uint64_t flags = irq_save();

	process->wait_next = fd_wait_head;
	fd_wait_head = process;

	irq_restore(flags);
}

void fd_wait_remove(process_t* process)
{
	if (process == NULL)
	{
		return;
	}

	uint64_t flags = irq_save();

	process_t* previous = NULL;
	process_t* current = fd_wait_head;

	while (current != NULL)
	{
		if (current == process)
		{
			if (previous == NULL)
			{
				fd_wait_head = current->wait_next;
			}
			else
			{
				previous->wait_next = current->wait_next;
			}
			current->wait_next = NULL;
			break;
		}
		previous = current;
		current = current->wait_next;
	}

	irq_restore(flags);
}

void fd_wait_wake_all(void)
{
	uint64_t flags = irq_save();

	process_t* process = fd_wait_head;
	fd_wait_head = NULL;

	while (process != NULL)
	{
		process_t* next = process->wait_next;
		process->wait_next = NULL;
		scheduler_wake(process);
		process = next;
	}

	irq_restore(flags);
}
