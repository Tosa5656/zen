#include "cpu.h"

uint8_t syscall_stack[16384] __attribute__((aligned(16)));

cpu_local_t cpu_locals[1] __attribute__((aligned(64))) =
{
	{ .kernel_rsp = (uint64_t)SYSCALL_STACK_TOP },
};
