#ifndef MSR_H
#define MSR_H

#include <stdint.h>

#define IA32_EFER_MSR           0xC0000080
#define IA32_STAR_MSR           0xC0000081
#define IA32_LSTAR_MSR          0xC0000082
#define IA32_FMASK_MSR          0xC0000084
#define IA32_GS_BASE_MSR          0xC0000101
#define IA32_KERNEL_GS_BASE_MSR 0xC0000102

#define EFER_SCE (1ULL << 0)

static inline uint64_t read_msr(uint32_t msr)
{
	uint32_t low;
	uint32_t high;

	__asm__ volatile("rdmsr" : "=a"(low), "=d"(high) : "c"(msr));

	return ((uint64_t)high << 32) | low;
}

static inline void write_msr(uint32_t msr, uint64_t value)
{
	__asm__ volatile("wrmsr" :: "c"(msr), "a"((uint32_t)value), "d"((uint32_t)(value >> 32)));
}

#endif
