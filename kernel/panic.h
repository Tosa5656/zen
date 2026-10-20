#ifndef PANIC_H
#define PANIC_H

#include <stdint.h>

void kernel_panic(const char *message);
void kernel_panic_value(const char *message, uint64_t value);

#endif