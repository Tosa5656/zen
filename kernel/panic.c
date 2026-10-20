#include "panic.h"
#include "serial.h"

void kernel_panic(const char *message)
{
	serial_write("\r\n!!! KERNEL PANIC !!!\r\n");
	if (message != 0)
	{
		serial_write(message);
	}
	serial_write("\r\n");

	__asm__ volatile("cli");
	while (1)
	{
		__asm__ volatile("hlt");
	}
}

void kernel_panic_value(const char *message, uint64_t value)
{
	serial_write("\r\n!!! KERNEL PANIC !!!\r\n");
	if (message != 0)
	{
		serial_write(message);
	}
	serial_write("\r\n");
	serial_field("Value: ", value);

	__asm__ volatile("cli");
	while (1)
	{
		__asm__ volatile("hlt");
	}
}
