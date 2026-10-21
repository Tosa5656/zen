#ifndef BLOCK_DEVICE_H
#define BLOCK_DEVICE_H

#include <stdint.h>

#define MAX_BLOCK_DEVICES 4

typedef struct block_device
{
	uint32_t id;
	char name[32];
	uint64_t total_sectors;

	int (*read_blocks)(struct block_device* dev, uint64_t lba, uint32_t count, void* buffer);
	int (*write_blocks)(struct block_device* dev, uint64_t lba, uint32_t count, void* buffer);

	void* private_data;
} block_device_t;

void block_device_register(block_device_t* dev);

#endif
