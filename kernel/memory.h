#pragma once

#ifndef MEMORY_H
#define MEMORY_H

#include <stdint.h>
#include <stddef.h>

#include "boot_info.h"
#include "serial.h"

#define PAGE_SIZE 4096
#define PHYSICAL_MEMORY_MANAGER_BITMAP_BASE 0x400000ULL

#define MEMORY_TYPE_USABLE 1
#define MEMORY_TYPE_RESERVED 2
#define MEMORY_TYPE_ACPI_RECLAIMABLE 3
#define MEMORY_TYPE_ACPI_NVS 4
#define MEMORY_TYPE_UNUSABLE 5
#define MEMORY_TYPE_PERSISTENT 6

#define KERNEL_PHYSICAL_MAP_BASE 0xFFFF800000000000ULL

uint8_t* physical_memory_manager_bitmap = NULL;
uint64_t physical_memory_manager_bitmap_size = 0;
uint64_t total_pages = 0;

typedef struct
{
        uint64_t base_address;
        uint64_t length;
        uint32_t type;
        uint32_t extended_attributes;
} __attribute__((packed)) memory_map_entry_t;

uint64_t find_maximum_physical_memory(const boot_info_t *info)
{
	memory_map_entry_t *memory_map = (memory_map_entry_t*)info->memory_map_address;
	uint64_t max_address = 0;

	for(uint64_t i = 0; i < info->memory_map_entries; i++)
	{
		serial_field("Region Base", memory_map[i].base_address);
        	serial_field("Region Length", memory_map[i].length);
        	serial_field("Region Type", memory_map[i].type);

		if(memory_map[i].type == MEMORY_TYPE_USABLE)
		{
			uint64_t region_end = memory_map[i].base_address + memory_map[i].length;
			if(region_end > max_address)
			{
				max_address = region_end;
			}
		}
	}

	return max_address;
}

void physical_memory_manager_init(const boot_info_t *info)
{
	memory_map_entry_t *memory_map = (memory_map_entry_t*)info->memory_map_address;
	uint64_t max_physical_address = 0;

	for(uint64_t i = 0; i < info->memory_map_entries; i++)
	{
		if(memory_map[i].type == MEMORY_TYPE_USABLE)
		{
			uint64_t region_end = memory_map[i].base_address + memory_map[i].length;
			if(region_end > max_physical_address)
			{
				max_physical_address = region_end;
			}
		}
	}

	total_pages = max_physical_address / PAGE_SIZE;
	physical_memory_manager_bitmap_size = total_pages / 8;
	if (total_pages % 8 != 0) physical_memory_manager_bitmap_size++;

	uint64_t bitmap_physical_address = 0;
	for(uint64_t i = 0; i < info->memory_map_entries; i++)
	{
		if(memory_map[i].type != MEMORY_TYPE_USABLE)
		{
			continue;
		}

		uint64_t region_start = memory_map[i].base_address;
		uint64_t region_end = region_start + memory_map[i].length;

		if(PHYSICAL_MEMORY_MANAGER_BITMAP_BASE >= region_start && PHYSICAL_MEMORY_MANAGER_BITMAP_BASE + physical_memory_manager_bitmap_size <= region_end)
		{
			bitmap_physical_address = PHYSICAL_MEMORY_MANAGER_BITMAP_BASE;
			break;
		}
	}

	if(bitmap_physical_address == 0)
	{
		serial_write("Could not find free RAM for PMM Bitmap!\n");
		return;
	}

	physical_memory_manager_bitmap = (uint8_t*)bitmap_physical_address;

	serial_field("Max physical address: ", max_physical_address);
	serial_field("Total pages total: ", total_pages);
	serial_field("Bitmap size: ", physical_memory_manager_bitmap_size);
	serial_field("Bitmap phys allocation address: ", bitmap_physical_address);

	for (uint64_t i = 0; i < physical_memory_manager_bitmap_size; i++)
	{
		physical_memory_manager_bitmap[i] = 0xFF;
    	}
}

#endif
