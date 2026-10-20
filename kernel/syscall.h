#ifndef SYSCALL_H
#define SYSCALL_H

#include "idt.h"

void syscall_init(void);
uint64_t syscall_dispatch(cpu_registers_t *registers);

#endif
