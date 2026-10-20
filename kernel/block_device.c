#include "block_device.h"
#include "serial.h"

block_device_t* registry[MAX_BLOCK_DEVICES];
uint32_t device_count = 0;

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
