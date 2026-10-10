#ifndef BOOT_INFO_H
#define BOOT_INFO_H

#include <stdint.h>

typedef struct
{
	uint64_t base_address;
	uint64_t length;
	uint32_t type;
	uint32_t extended_attributes;
} __attribute__((packed)) memory_map_entry_t;

typedef struct
{
	uint32_t magic;
	uint32_t size;
	uint64_t memory_map_address;
	uint64_t memory_map_entries;
	uint64_t framebuffer_address;
	uint32_t framebuffer_width;
	uint32_t framebuffer_height;
	uint32_t framebuffer_pitch;
	uint8_t framebuffer_bpp;
	uint8_t framebuffer_red_mask_shift;
	uint8_t framebuffer_green_mask_shift;
	uint8_t framebuffer_blue_mask_shift;
	uint8_t _reserved1[4];
	uint64_t acpi_rsdp_address;
	uint64_t smbios_address;
} __attribute__((packed)) boot_info_t;

#endif
