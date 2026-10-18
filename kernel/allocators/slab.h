#ifndef SLAB_H
#define SLAB_H

#include <stdint.h>

#define SLAB_CLASS_COUNT 7
#define SLAB_MAX_OBJECT  1024

void slab_init(void);

void *slab_alloc(uint64_t size);
void slab_free(void *pointer);

int slab_owns(const void *pointer);
uint64_t slab_usable_size(const void *pointer);

void slab_print_stats(void);
void slab_print_details(void);

#endif
