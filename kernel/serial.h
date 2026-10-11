#ifndef SERIAL_H
#define SERIAL_H

#include <stdint.h>

void serial_init(void);
void serial_putc(char c);
void serial_write(const char *s);
void serial_hex(uint64_t value);
void serial_dec(uint64_t value);
void serial_field(const char *name, uint64_t value);

#endif
