#include "acpi.h"
#include "memory.h"
#include "serial.h"

static acpi_header_t *acpi_root_table = NULL;
static uint32_t acpi_entry_count = 0;
static uint8_t acpi_entry_size = 0;

static bool acpi_checksum_ok(const uint8_t *data, uint32_t length)
{
	uint8_t sum = 0;

	for (uint32_t i = 0; i < length; i++)
	{
		sum = (uint8_t)(sum + data[i]);
	}

	return sum == 0;
}

static bool acpi_signature_equals(const char *signature, const char *expected, uint32_t length)
{
	for (uint32_t i = 0; i < length; i++)
	{
		if (signature[i] != expected[i])
		{
			return false;
		}
	}

	return true;
}

static void serial_write_signature(const char *signature)
{
	for (int i = 0; i < 4; i++)
	{
		serial_putc(signature[i]);
	}
}

bool acpi_validate_header(acpi_header_t *header)
{
	if (header == NULL || header->length < sizeof(acpi_header_t))
	{
		return false;
	}

	return acpi_checksum_ok((const uint8_t *)header, header->length);
}

acpi_header_t *acpi_find_table(const char *signature)
{
	if (acpi_root_table == NULL)
	{
		return NULL;
	}

	const uint8_t *table_pointers = (const uint8_t *)acpi_root_table + sizeof(acpi_header_t);

	for (uint32_t i = 0; i < acpi_entry_count; i++)
	{
		uint64_t table_physical;

		if (acpi_entry_size == sizeof(uint64_t))
		{
			table_physical = ((const uint64_t *)table_pointers)[i];
		}
		else
		{
			table_physical = ((const uint32_t *)table_pointers)[i];
		}

		if (table_physical == 0)
		{
			continue;
		}

		acpi_header_t *table = (acpi_header_t *)physical_to_virtual(table_physical);

		if (acpi_signature_equals(table->signature, signature, 4) && acpi_validate_header(table))
		{
			return table;
		}
	}

	return NULL;
}

acpi_madt_t *acpi_get_madt(void)
{
	return (acpi_madt_t *)acpi_find_table("APIC");
}

static acpi_madt_info_t madt_info = {0};

acpi_madt_info_t *acpi_get_madt_info(void)
{
	return &madt_info;
}

void acpi_parse_madt(void)
{
	acpi_madt_t *madt = acpi_get_madt();

	if (madt == NULL)
	{
		serial_write("ACPI: MADT not found!\r\n");
		return;
	}

	madt_info.local_apic_address = madt->local_apic_address;
	madt_info.cpu_count = 0;
	madt_info.ioapic_count = 0;
	madt_info.interrupt_source_count = 0;

	const uint8_t *pointer = madt->entries;
	const uint8_t *end = (const uint8_t *)madt + madt->header.length;

	while (pointer + sizeof(madt_entry_header_t) <= end)
	{
		madt_entry_header_t *entry = (madt_entry_header_t *)pointer;

		if (entry->length < sizeof(madt_entry_header_t))
		{
			break;
		}

		switch (entry->type)
		{
			case MADT_TYPE_LOCAL_APIC:
			{
				madt_local_apic_t *local_apic = (madt_local_apic_t *)entry;
				if (local_apic->flags & 1)
				{
					madt_info.cpu_count++;
				}
				break;
			}
			case MADT_TYPE_IOAPIC:
			{
				madt_ioapic_t *ioapic = (madt_ioapic_t *)entry;
				if (madt_info.ioapic_count < ACPI_MAX_IOAPICS)
				{
					acpi_ioapic_t *dst = &madt_info.ioapics[madt_info.ioapic_count++];
					dst->id = ioapic->ioapic_id;
					dst->address = ioapic->ioapic_address;
					dst->global_system_interrupt_base = ioapic->global_system_interrupt_base;
				}
				break;
			}
			case MADT_TYPE_INTERRUPT_SOURCE_OVERRIDE:
			{
				madt_interrupt_source_override_t *override = (madt_interrupt_source_override_t *)entry;
				if (madt_info.interrupt_source_count < ACPI_MAX_INTERRUPT_SOURCES)
				{
					acpi_interrupt_source_t *dst = &madt_info.interrupt_sources[madt_info.interrupt_source_count++];
					dst->irq_source = override->irq_source;
					dst->global_system_interrupt = override->global_system_interrupt;
					dst->flags = override->flags;
				}
				break;
			}
			case MADT_TYPE_NMI:
			{
				madt_nmi_t *nmi = (madt_nmi_t *)entry;
				serial_write("ACPI: MADT NMI processor ");
				serial_dec(nmi->acpi_processor_id);
				serial_write(" LINT ");
				serial_dec(nmi->local_apic_lint);
				serial_write("\r\n");
				break;
			}
			case MADT_TYPE_LOCAL_APIC_OVERRIDE:
			{
				madt_local_apic_override_t *override = (madt_local_apic_override_t *)entry;
				madt_info.local_apic_address = override->local_apic_address;
				break;
			}
			default:
				break;
		}

		pointer += entry->length;
	}

	serial_field("ACPI: MADT CPU count ", madt_info.cpu_count);
	serial_field("ACPI: MADT local APIC address ", madt_info.local_apic_address);

	for (uint32_t i = 0; i < madt_info.ioapic_count; i++)
	{
		serial_write("ACPI: MADT IOAPIC id ");
		serial_dec(madt_info.ioapics[i].id);
		serial_write(", address ");
		serial_hex(madt_info.ioapics[i].address);
		serial_write(", GSI base ");
		serial_dec(madt_info.ioapics[i].global_system_interrupt_base);
		serial_write("\r\n");
	}

	for (uint32_t i = 0; i < madt_info.interrupt_source_count; i++)
	{
		serial_write("ACPI: MADT IRQ ");
		serial_dec(madt_info.interrupt_sources[i].irq_source);
		serial_write(" -> GSI ");
		serial_dec(madt_info.interrupt_sources[i].global_system_interrupt);
		serial_write("\r\n");
	}
}

void acpi_init(const boot_info_t *info)
{
	serial_write("ACPI: initializing...\r\n");

	if (info == NULL || info->acpi_rsdp_address == 0)
	{
		serial_write("ACPI: RSDP not found in boot_info!\r\n");
		return;
	}

	acpi_rsdp_t *rsdp = (acpi_rsdp_t *)physical_to_virtual(info->acpi_rsdp_address);

	if (!acpi_signature_equals(rsdp->signature, "RSD PTR ", 8))
	{
		serial_write("ACPI: invalid RSDP signature!\r\n");
		return;
	}

	if (!acpi_checksum_ok((const uint8_t *)rsdp, 20))
	{
		serial_write("ACPI: RSDP checksum failed!\r\n");
		return;
	}

	serial_field("ACPI: RSDP revision ", rsdp->revision);

	acpi_header_t *root;
	const char *root_name;
	uint8_t entry_size;

	if (rsdp->revision >= 2 && rsdp->xsdt_address != 0)
	{
		if (rsdp->length < 36 || rsdp->length > 4096 ||
		    !acpi_checksum_ok((const uint8_t *)rsdp, rsdp->length))
		{
			serial_write("ACPI: extended RSDP checksum failed!\r\n");
			return;
		}

		root = (acpi_header_t *)physical_to_virtual(rsdp->xsdt_address);
		root_name = "XSDT";
		entry_size = sizeof(uint64_t);
	}
	else
	{
		root = (acpi_header_t *)physical_to_virtual(rsdp->rsdt_address);
		root_name = "RSDT";
		entry_size = sizeof(uint32_t);
	}

	if (!acpi_signature_equals(root->signature, root_name, 4))
	{
		serial_write("ACPI: invalid root table signature!\r\n");
		return;
	}

	if (!acpi_validate_header(root))
	{
		serial_write("ACPI: root table checksum failed!\r\n");
		return;
	}

	uint32_t entries = (root->length - sizeof(acpi_header_t)) / entry_size;

	acpi_root_table = root;
	acpi_entry_count = entries;
	acpi_entry_size = entry_size;

	serial_write("ACPI: ");
	serial_write(root_name);
	serial_write(" contains ");
	serial_dec(entries);
	serial_write(" entries\r\n");

	const uint8_t *table_pointers = (const uint8_t *)root + sizeof(acpi_header_t);

	for (uint32_t i = 0; i < entries; i++)
	{
		uint64_t table_physical;

		if (entry_size == sizeof(uint64_t))
		{
			table_physical = ((const uint64_t *)table_pointers)[i];
		}
		else
		{
			table_physical = ((const uint32_t *)table_pointers)[i];
		}

		if (table_physical == 0)
		{
			continue;
		}

		acpi_header_t *table = (acpi_header_t *)physical_to_virtual(table_physical);

		serial_write("ACPI: table ");
		serial_write_signature(table->signature);
		serial_write(acpi_validate_header(table) ? " [ok]\r\n" : " [bad checksum]\r\n");
	}

	acpi_parse_madt();

	serial_write("ACPI: init done\r\n");
}
