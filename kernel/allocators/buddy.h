#ifndef BUDDY_H
#define BUDDY_H

#include <stdint.h>

#define BUDDY_MAX_ORDER 13

void buddy_init(uint64_t max_physical_address);

uint64_t buddy_alloc_page(void);
uint64_t buddy_alloc_pages(uint64_t count);
void buddy_free_page(uint64_t physical_address);
void buddy_free_pages(uint64_t physical_address, uint64_t count);
uint64_t buddy_get_free_pages(void);
void buddy_lock_range(uint64_t physical_address, uint64_t size);
void buddy_free_range(uint64_t physical_address, uint64_t size);
void buddy_mark_slab(uint64_t physical_address, uint64_t order);
int buddy_page_is_slab(uint64_t physical_address);
uint64_t buddy_page_order(uint64_t physical_address);

void buddy_print_stats(void);

#endif
