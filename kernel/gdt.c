#include "gdt.h"
#include "serial.h"

_Static_assert(sizeof(gdt_entry_t) == 8, "gdt_entry size");
_Static_assert(sizeof(gdt_tss_description_t) == 16, "tss_desc size");
_Static_assert(sizeof(tss_t) == 104, "tss size");

extern void gdt_flush(gdtr_t *g);
extern void tss_flush(uint16_t sel);

static gdt_t gdt __attribute__((aligned(16)));
static tss_t tss __attribute__((aligned(16)));

static uint8_t df_stack[4096] __attribute__((aligned(16)));
static uint8_t boot_kstack[16384] __attribute__((aligned(16)));

static void set_entry(int i, uint8_t access, uint8_t flags)
{
	gdt.entries[i] = (gdt_entry_t){
		.limit_low = 0xFFFF,
		.access = access,
		.limit_high_flags = (uint8_t)((flags << 4) | 0x0F),
	};
}

static void set_tss(uint64_t base, uint32_t limit)
{
	gdt.tss = (gdt_tss_description_t){
		.limit_low = limit & 0xFFFF,
		.base_low = base & 0xFFFF,
		.base_mid = (base >> 16) & 0xFF,
		.access = 0x89,
		.limit_high_flags = (limit >> 16) & 0x0F,
		.base_high = (base >> 24) & 0xFF,
		.base_upper = (uint32_t)(base >> 32),
	};
}

void gdt_init(void)
{
	gdt.entries[0] = (gdt_entry_t){0};

	set_entry(1, 0x9A, 0xA);
	set_entry(2, 0x92, 0xC);
	set_entry(3, 0xF2, 0xC);
	set_entry(4, 0xFA, 0xA);

	tss.rsp[0] = (uint64_t)(boot_kstack + sizeof boot_kstack);
	tss.ist[IST_DOUBLE_FAULT - 1] = (uint64_t)(df_stack + sizeof df_stack);
	tss.iopb_offset = sizeof(tss_t);

	set_tss((uint64_t)&tss, sizeof(tss_t) - 1);

	gdtr_t g = { sizeof(gdt) - 1, (uint64_t)&gdt };
	gdt_flush(&g);
	tss_flush(GDT_TSS);

	serial_write("GDT inited!\r\n");
}

void tss_set_rsp0(uint64_t rsp0)
{
	tss.rsp[0] = rsp0;
}
