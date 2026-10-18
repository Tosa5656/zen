#ifndef HEAP_H
#define HEAP_H

#include <stdint.h>
#include "boot_info.h"

void heap_init(const boot_info_t *info);

void *kmalloc(uint64_t size);
void *kcalloc(uint64_t count, uint64_t size);
void *krealloc(void *pointer, uint64_t size);
void kfree(void *pointer);

void heap_print_stats(void);

#endif