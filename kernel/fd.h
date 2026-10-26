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

#define FD_FCNTL_DUPFD         0
#define FD_FCNTL_GETFD         1
#define FD_FCNTL_SETFD         2
#define FD_FCNTL_GETFL         3
#define FD_FCNTL_SETFL         4
#define FD_FCNTL_DUPFD_CLOEXEC 1030

#define FD_FCNTL_O_RDONLY 0
#define FD_FCNTL_O_WRONLY 1
#define FD_FCNTL_O_RDWR   2

struct thread;

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
int fd_open(const char* path, uint32_t flags);
uint64_t fd_read(int fd, void* buffer, uint64_t count);
uint64_t fd_write(int fd, const void* buffer, uint64_t count);
int fd_close(int fd);
int fd_dup(int fd);
int fd_seek(int fd, int64_t offset, int whence);
int fd_dup_from(int fd, int start);
int fd_fcntl(int fd, int cmd, uint64_t arg);
void fd_wait_add(struct thread* thread);
void fd_wait_remove(struct thread* thread);
void fd_wait_wake_all(void);

#endif
