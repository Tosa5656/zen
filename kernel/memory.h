#pragma once

#ifndef MEMORY_H
#define MEMORY_H

#include <stdint.h>
#include <stddef.h>

#include "boot_info.h"

#define PAGE_SIZE 4096
#define PAGE_PRESENT (1ULL << 0)
#define PAGE_WRITABLE (1ULL << 1)
#define PAGE_USER (1ULL << 2)
#define PAGE_WRITE_THROUGH (1ULL << 3)
#define PAGE_CACHE_DISABLE (1ULL << 4)
#define PAGE_HUGE (1ULL << 7)

#define MEMORY_TYPE_USABLE 1
#define MEMORY_TYPE_RESERVED 2
#define MEMORY_TYPE_ACPI_RECLAIMABLE 3
#define MEMORY_TYPE_ACPI_NVS 4
#define MEMORY_TYPE_UNUSABLE 5
#define MEMORY_TYPE_PERSISTENT 6

#define KERNEL_PHYSICAL_MAP_BASE 0xFFFF800000000000ULL
#define KERNEL_VIRTUAL_BASE 0xFFFFFFFF80000000ULL
#define KERNEL_IMAGE_VIRTUAL (KERNEL_VIRTUAL_BASE + 0x100000ULL)
#define VIRTUAL_TO_PHYSICAL(address) ((uint64_t)(address) - KERNEL_VIRTUAL_BASE)
#define KERNEL_MMIO_BASE 0xFFFFFE0000000000ULL

#define USER_SPACE_END 0x0000800000000000ULL

extern uint8_t _kernel_start[];
extern uint8_t _kernel_end[];

typedef uint64_t pml4_table_t[512];
typedef uint64_t pdpt_table_t[512];
typedef uint64_t pd_table_t[512];
typedef uint64_t pt_table_t[512];

typedef struct
{
        uint64_t base_address;
        uint64_t length;
        uint32_t type;
        uint32_t extended_attributes;
} __attribute__((packed)) memory_map_entry_t;

extern uint64_t boot_alloc_current;
extern __attribute__((aligned(4096))) pml4_table_t kernel_pml4;

static inline void *physical_to_virtual(uint64_t physical)
{
	return (void *)(physical + KERNEL_PHYSICAL_MAP_BASE);
}

void boot_alloc_init(void);
uint64_t allocate_boot_page(void);

uint64_t physical_memory_manager_get_max_ram(const boot_info_t *info);
void physical_memory_manager_lock_region(uint64_t physical_address, uint64_t size);
void physical_memory_manager_free_region(uint64_t physical_address, uint64_t size);
void physical_memory_manager_free_acpi_reclaimable(const boot_info_t *info);
void physical_memory_manager_lock_protected_regions(const boot_info_t *info);
uint64_t physical_memory_manager_alloc_page(void);
uint64_t physical_memory_manager_alloc_pages(uint64_t count);
void physical_memory_manager_free_page(uint64_t physical_address);
void physical_memory_manager_free_pages(uint64_t physical_address, uint64_t count);
uint64_t physical_memory_manager_get_free_pages(void);
void physical_memory_manager_init(const boot_info_t *info);

void virtual_memory_manager_map_page(uint64_t virt, uint64_t physical, uint64_t flags);
void virtual_memory_manager_map_range(uint64_t virt, uint64_t physical, uint64_t size, uint64_t flags);
uint64_t virtual_memory_manager_map_anonymous(uint64_t virt, uint64_t size, uint64_t flags);
void virtual_memory_manager_unmap_range(uint64_t virt, uint64_t size);
uint64_t virtual_memory_manager_get_physical(uint64_t virt);
void virtual_memory_manager_init(const boot_info_t *info, uint64_t max_ram, uint64_t kernel_size);
uint64_t virtual_memory_manager_create_address_space(void);
void virtual_memory_manager_destroy_address_space(uint64_t pml4_physical);
void virtual_memory_manager_switch_address_space(uint64_t pml4_physical);
uint64_t virtual_memory_manager_get_current_pml4(void);
uint64_t virtual_memory_manager_get_kernel_pml4(void);
int virtual_memory_manager_map_user_page(uint64_t pml4_physical, uint64_t virt, uint64_t physical, uint64_t flags);
void virtual_memory_manager_unmap_user_page(uint64_t pml4_physical, uint64_t virt);
uint64_t virtual_memory_manager_translate(uint64_t pml4_physical, uint64_t virt);

#endif
