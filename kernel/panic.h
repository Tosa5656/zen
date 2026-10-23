#ifndef PANIC_H
#define PANIC_H

#include <stdint.h>

__attribute__((noreturn)) void kernel_panic(const char *message);
__attribute__((noreturn)) void kernel_panic_value(const char *message, uint64_t value);
__attribute__((noreturn)) void kernel_panic_halt(void);

#endif
