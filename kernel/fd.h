#ifndef FD_H
#define FD_H

#include <stdint.h>
#include "spinlock.h"
#include "vfs.h"

#define FD_STDIN  0
#define FD_STDOUT 1
#define FD_STDERR 2
#define FD_MAX 64

#define FD_WOULD_BLOCK ((uint64_t)-2)

typedef struct vfs_fd
{
	vfs_node_t* node;
	uint64_t offset;
	uint32_t flags;
	uint32_t refs;
} vfs_fd_t;

typedef struct fd_table
{
	spinlock_t lock;
	vfs_fd_t* descriptors[FD_MAX];
} fd_table_t;

fd_table_t* fd_table_create_default(void);
void fd_table_destroy(fd_table_t* table);
fd_table_t* fd_table_current(void);
vfs_fd_t* fd_get(int fd);
int fd_open(const char* path, uint32_t flags);
uint64_t fd_read(int fd, void* buffer, uint64_t count);
uint64_t fd_write(int fd, const void* buffer, uint64_t count);
int fd_close(int fd);
int fd_dup(int fd);

#endif
