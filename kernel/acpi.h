#ifndef ACPI_H
#define ACPI_H

#include <stdint.h>
#include <stdbool.h>
#include "boot_info.h"

typedef struct
{
	char signature[4];
	uint32_t length;
	uint8_t revision;
	uint8_t checksum;
	char oem_id[6];
	char oem_table_id[8];
	uint32_t oem_revision;
	uint32_t creator_id;
	uint32_t creator_revision;
} __attribute__((packed)) acpi_header_t;

typedef struct
{
	char signature[8];
	uint8_t checksum;
	char oem_id[6];
	uint8_t revision;
	uint32_t rsdt_address;
	uint32_t length;
	uint64_t xsdt_address;
	uint8_t extended_checksum;
	uint8_t reserved[3];
} __attribute__((packed)) acpi_rsdp_t;

typedef struct
{
	acpi_header_t header;
	uint32_t tables[];
} __attribute__((packed)) acpi_rsdt_t;

typedef struct
{
	acpi_header_t header;
	uint64_t tables[];
} __attribute__((packed)) acpi_xsdt_t;

typedef struct
{
	uint8_t type;
	uint8_t length;
} __attribute__((packed)) madt_entry_header_t;

typedef struct
{
    acpi_header_t header;
    uint32_t local_apic_address;
    uint32_t flags;
    uint8_t entries[];
} __attribute__((packed)) acpi_madt_t;

#define MADT_TYPE_LOCAL_APIC 0
#define MADT_TYPE_IOAPIC 1
#define MADT_TYPE_INTERRUPT_SOURCE_OVERRIDE 2
#define MADT_TYPE_NMI 4
#define MADT_TYPE_LOCAL_APIC_OVERRIDE 5

typedef struct
{
	madt_entry_header_t header;
	uint8_t acpi_processor_id;
	uint8_t apic_id;
	uint32_t flags;
} __attribute__((packed)) madt_local_apic_t;

typedef struct
{
	madt_entry_header_t header;
	uint8_t ioapic_id;
	uint8_t reserved;
	uint32_t ioapic_address;
	uint32_t global_system_interrupt_base;
} __attribute__((packed)) madt_ioapic_t;

typedef struct
{
	madt_entry_header_t header;
	uint8_t bus_source;
	uint8_t irq_source;
	uint32_t global_system_interrupt;
	uint16_t flags;
} __attribute__((packed)) madt_interrupt_source_override_t;

typedef struct
{
	madt_entry_header_t header;
	uint8_t acpi_processor_id;
	uint16_t flags;
	uint8_t local_apic_lint;
} __attribute__((packed)) madt_nmi_t;

typedef struct
{
	madt_entry_header_t header;
	uint16_t reserved;
	uint64_t local_apic_address;
} __attribute__((packed)) madt_local_apic_override_t;

#define ACPI_MAX_IOAPICS 8
#define ACPI_MAX_INTERRUPT_SOURCES 16

typedef struct
{
	uint8_t id;
	uint32_t address;
	uint32_t global_system_interrupt_base;
} acpi_ioapic_t;

typedef struct
{
	uint8_t irq_source;
	uint32_t global_system_interrupt;
	uint16_t flags;
} acpi_interrupt_source_t;

typedef struct
{
	uint64_t local_apic_address;
	uint32_t cpu_count;
	uint32_t ioapic_count;
	uint32_t interrupt_source_count;
	acpi_ioapic_t ioapics[ACPI_MAX_IOAPICS];
	acpi_interrupt_source_t interrupt_sources[ACPI_MAX_INTERRUPT_SOURCES];
} acpi_madt_info_t;

bool acpi_validate_header(acpi_header_t* header);
void acpi_init(const boot_info_t *info);
void acpi_relocate_tables(void);

acpi_header_t *acpi_find_table(const char *signature);
acpi_madt_t *acpi_get_madt(void);
acpi_madt_info_t *acpi_get_madt_info(void);
void acpi_parse_madt(void);

#endif
