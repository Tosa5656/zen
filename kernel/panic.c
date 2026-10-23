#include "panic.h"
#include "serial.h"

void kernel_panic_halt(void)
{
	__asm__ volatile("cli");

	while (1)
	{
		__asm__ volatile("hlt");
	}
}

void kernel_panic(const char *message)
{
	__asm__ volatile("cli");

	serial_write("\r\n!!! KERNEL PANIC !!!\r\n");

	if (message != 0)
	{
		serial_write(message);
	}

	serial_write("\r\n");

	kernel_panic_halt();
}

void kernel_panic_value(const char *message, uint64_t value)
{
	__asm__ volatile("cli");

	serial_write("\r\n!!! KERNEL PANIC !!!\r\n");

	if (message != 0)
	{
		serial_write(message);
	}

	serial_write("\r\n");
	serial_field("Value: ", value);

	kernel_panic_halt();
}
