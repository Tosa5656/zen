#include "boot_info.h"

#define COM1 0x3F8

static inline void outb(uint16_t port, uint8_t value)
{
	__asm__ volatile("outb %0, %1" : : "a"(value), "Nd"(port));
}

static inline uint8_t inb(uint16_t port)
{
	uint8_t value;
	__asm__ volatile("inb %1, %0" : "=a"(value) : "Nd"(port));
	return value;
}

static void serial_init(void)
{
	outb(COM1 + 1, 0x00);
	outb(COM1 + 3, 0x80);
	outb(COM1 + 0, 0x01);
	outb(COM1 + 1, 0x00);
	outb(COM1 + 3, 0x03);
	outb(COM1 + 2, 0xC7);
	outb(COM1 + 4, 0x0B);
}

static void serial_putc(char c)
{
	while ((inb(COM1 + 5) & 0x20) == 0) outb(COM1, (uint8_t)c);
}

static void serial_write(const char *s)
{
	while (*s != '\0') serial_putc(*s++);
}

static void serial_hex(uint64_t value)
{
	static const char digits[] = "0123456789abcdef";

	serial_write("0x");
	for (int i = 15; i >= 0; i--)
		serial_putc(digits[(value >> (i * 4)) & 0xF]);
}

static void serial_dec(uint64_t value)
{
	char buffer[21];
	int i = 0;

	if (value == 0)
	{
		serial_putc('0');
		return;
	}
	while (value != 0)
	{
		buffer[i++] = (char)('0' + (value % 10));
		value /= 10;
	}
	while (i-- > 0)
		serial_putc(buffer[i]);
}

static void serial_field(const char *name, uint64_t value)
{
	serial_write(name);
	serial_write(" = ");
	serial_dec(value);
	serial_write("  (");
	serial_hex(value);
	serial_write(")\r\n");
}

static void dump_boot_info(const boot_info_t *info)
{
	if (info == 0)
	{
		serial_write("boot_info pointer is NULL!\r\n");
		return;
	}

	serial_field("magic                       ", info->magic);
	serial_field("size                        ", info->size);
	serial_field("memory_map_address          ", info->memory_map_address);
	serial_field("memory_map_entries          ", info->memory_map_entries);
	serial_field("framebuffer_address         ", info->framebuffer_address);
	serial_field("framebuffer_width           ", info->framebuffer_width);
	serial_field("framebuffer_height          ", info->framebuffer_height);
	serial_field("framebuffer_pitch           ", info->framebuffer_pitch);
	serial_field("framebuffer_bpp             ", info->framebuffer_bpp);
	serial_field("framebuffer_red_mask_shift  ", info->framebuffer_red_mask_shift);
	serial_field("framebuffer_green_mask_shift", info->framebuffer_green_mask_shift);
	serial_field("framebuffer_blue_mask_shift ", info->framebuffer_blue_mask_shift);
	serial_field("acpi_rsdp_address           ", info->acpi_rsdp_address);
	serial_field("smbios_address              ", info->smbios_address);
}

void kmain(boot_info_t *boot_info)
{
	serial_init();
	dump_boot_info(boot_info);
}
