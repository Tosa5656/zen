#ifndef CPU_H
#define CPU_H

#define CPU_LOCAL_KERNEL_RSP        0
#define CPU_LOCAL_USER_RSP          8
#define CPU_LOCAL_KERNEL_RIP        16
#define CPU_LOCAL_KERNEL_SAVED_RSP  24

#include <stdint.h>
#include <stddef.h>

typedef struct
{
	uint64_t kernel_rsp;
	uint64_t user_rsp;
	uint64_t kernel_rip;
	uint64_t kernel_saved_rsp;
} __attribute__((aligned(64))) cpu_local_t;

_Static_assert(offsetof(cpu_local_t, kernel_rsp) == CPU_LOCAL_KERNEL_RSP, "cpu_local: kernel_rsp");
_Static_assert(offsetof(cpu_local_t, user_rsp) == CPU_LOCAL_USER_RSP, "cpu_local: user_rsp");
_Static_assert(offsetof(cpu_local_t, kernel_rip) == CPU_LOCAL_KERNEL_RIP, "cpu_local: kernel_rip");
_Static_assert(offsetof(cpu_local_t, kernel_saved_rsp) == CPU_LOCAL_KERNEL_SAVED_RSP, "cpu_local: kernel_saved_rsp");

extern cpu_local_t cpu_locals[1];
extern uint8_t syscall_stack[16384] __attribute__((aligned(16)));

#define SYSCALL_STACK_TOP (syscall_stack + sizeof syscall_stack)
#define cpu_current() (&cpu_locals[0])

#endif
