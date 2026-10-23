#ifndef VFS_H
#define VFS_H

#include <stdint.h>
#include <stddef.h>
#include "block_device.h"

#define VFS_NAMELENGTH 256
#define VFS_MAX_DRIVERS 8
#define VFS_MAX_MOUNTS 8

#define VFS_RDONLY 0x1
#define VFS_WRONLY 0x2
#define VFS_RDWR   0x3
#define VFS_CREATE 0x4

#define VFS_SEEK_SET 0
#define VFS_SEEK_CUR 1
#define VFS_SEEK_END 2

typedef enum
{
	VFS_FILE = 1,
	VFS_DIRECTORY,
	VFS_CHARDEVICE,
	VFS_BLOCKDEVICE,
	VFS_SYMLINK
} vfs_type_t;

struct vfs_node;

typedef struct vfs_operations
{
	int (*read)(struct vfs_node* node, uint64_t offset, uint32_t size, void* buffer);
	int (*write)(struct vfs_node* node, uint64_t offset, uint32_t size, const void* buffer);
	struct vfs_node* (*finddir)(struct vfs_node* node, const char* name);
	int (*readdir)(struct vfs_node* node, uint32_t index, struct vfs_node* out_node);
	int (*create)(struct vfs_node* parent, const char* name, uint16_t mask);
	int (*mkdir)(struct vfs_node* parent, const char* name, uint16_t mask);
	int (*unlink)(struct vfs_node* parent, const char* name);
	int (*rmdir)(struct vfs_node* parent, const char* name);
	void (*release)(struct vfs_node* node);
} vfs_operations_t;

typedef struct vfs_node
{
	char name[VFS_NAMELENGTH];
	vfs_type_t type;
	uint64_t size;
	uint32_t inode_num;
	uint32_t refs;
	vfs_operations_t* operations;
	void* private_data;
	struct vfs_node* canonical;
	int orphaned;
} vfs_node_t;

typedef struct fs_driver
{
	const char* name;
	vfs_node_t* (*mount)(block_device_t* dev);
} fs_driver_t;

typedef struct vfs_mount
{
	char name[VFS_NAMELENGTH];
	vfs_node_t* node;
} vfs_mount_t;

typedef int (*vfs_dir_visitor_t)(const vfs_node_t* entry, void* context);

void vfs_init(void);
void fs_register(fs_driver_t* driver);
int vfs_mount(const char* name, block_device_t* dev);
int vfs_mount_root(block_device_t* dev);
void vfs_node_get(vfs_node_t* node);
void vfs_node_put(vfs_node_t* node);
vfs_node_t* vfs_open(const char* path, uint32_t flags);
int vfs_exists(const char* path);
int vfs_mkdir(const char* path);
int vfs_unlink(const char* path);
int vfs_rmdir(const char* path);
int vfs_node_read(vfs_node_t* node, uint64_t offset, uint32_t size, void* buffer);
int vfs_node_write(vfs_node_t* node, uint64_t offset, uint32_t size, const void* buffer);
void vfs_close(vfs_node_t* node);
int vfs_read(vfs_node_t* node, uint64_t offset, uint32_t size, void* buffer);
int vfs_write(vfs_node_t* node, uint64_t offset, uint32_t size, const void* buffer);
int vfs_list_directory(const char* path, vfs_dir_visitor_t visitor, void* context);
uint32_t vfs_count_directory(const char* path);
int64_t vfs_read_file(const char* path, void* buffer, uint32_t size);
int64_t vfs_write_file(const char* path, const void* buffer, uint32_t size, uint32_t flags);

#endif
