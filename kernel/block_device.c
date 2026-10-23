#include "block_device.h"
#include "serial.h"

#include <stddef.h>

block_device_t* registry[MAX_BLOCK_DEVICES];
uint32_t device_count = 0;

block_device_t* block_device_get(uint32_t index)
{
	return (index < device_count) ? registry[index] : NULL;
}

block_device_t* block_device_find(const char* name)
{
	if (name == NULL)
	{
		return NULL;
	}

	for (uint32_t i = 0; i < device_count; i++)
	{
		const char* a = registry[i]->name;
		const char* b = name;

		while (*a != '\0' && *a == *b)
		{
			a++;
			b++;
		}

		if (*a == *b)
		{
			return registry[i];
		}
	}

	return NULL;
}

uint32_t block_device_get_count(void)
{
	return device_count;
}

void block_device_register(block_device_t* dev)
{
	if (device_count < MAX_BLOCK_DEVICES)
	{
		registry[device_count++] = dev;
		serial_write("Storage Manager: Registered device: ");
		serial_write(dev->name);
		serial_write("\r\n");
	}
}
