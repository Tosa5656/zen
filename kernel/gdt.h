#ifndef GDT_H
#define GDT_H

#include <stdint.h>

#define GDT_KCODE   0x08
#define GDT_KDATA   0x10
#define GDT_UDATA   (0x18 | 3)
#define GDT_UCODE   (0x20 | 3)
#define GDT_TSS     0x28

#define IST_DOUBLE_FAULT 1

typedef struct
{
	uint16_t limit_low;
	uint16_t base_low;
	uint8_t base_mid;
	uint8_t access;
	uint8_t limit_high_flags;
	uint8_t base_high;
} __attribute__((packed)) gdt_entry_t;

typedef struct
{
	uint16_t limit_low;
	uint16_t base_low;
	uint8_t  base_mid;
	uint8_t  access;
	uint8_t  limit_high_flags;
	uint8_t  base_high;
	uint32_t base_upper;
	uint32_t reserved;
} __attribute__((packed)) gdt_tss_description_t;

typedef struct
{
	gdt_entry_t entries[5];
	gdt_tss_description_t tss;
} __attribute__((packed)) gdt_t;

typedef struct
{
	uint16_t limit;
	uint64_t base;
} __attribute__((packed)) gdtr_t;

typedef struct
{
	uint32_t reserved0;
	uint64_t rsp[3];
	uint64_t reserved1;
	uint64_t ist[7];
	uint64_t reserved2;
	uint16_t reserved3;
	uint16_t iopb_offset;
} __attribute__((packed)) tss_t;

void gdt_init(void);
void tss_set_rsp0(uint64_t rsp0);

#endif
