#include "ata.h"
#include "io.h"
#include "serial.h"
#include "spinlock.h"
#include "mbr.h"
#include "gpt.h"

#define MAX_PARTITIONS 128
#define ATA_WAIT_TIMEOUT 2000000

static spinlock_t ata_lock = {0};
static block_device_t ata_physical_devices[4];
static ata_private_t ata_private_data[4];
static uint32_t phys_disk_count = 0;
static block_device_t partition_devices[MAX_PARTITIONS];
static uint32_t partition_count = 0;

/* TODO: Remove to libc */
static void kernel_itoa(uint64_t value, char* str, int base)
{
    char *rc, *ptr;
    char tmp[64];
    int c;

    ptr = tmp;
    do {
        c = value % base;
        *ptr++ = (c < 10) ? (c + '0') : (c - 10 + 'a');
    } while (value /= base);

    rc = str;
    while (ptr > tmp) {
        *rc++ = *--ptr;
    }
    *rc = '\0';
}

static void kernel_strcat_n(char* dest, const char* src, uint32_t cap)
{
    uint32_t len = 0;
    while (len < cap && dest[len] != '\0') len++;
    while (len + 1 < cap && *src != '\0')
    {
        dest[len++] = *src++;
    }
    dest[len] = '\0';
}

void ata_init(void)
{
	serial_write("ATA: Scanning IDE Drives...\r\n");

	uint16_t bases[4] = { ATA_PRIMARY_BASE, ATA_PRIMARY_BASE, ATA_SECONDARY_BASE, ATA_SECONDARY_BASE };
	uint8_t heads[4]  = { 0xE0, 0xF0, 0xE0, 0xF0 }; // 0xE0 = Master (LBA), 0xF0 = Slave (LBA)
	const char* names[4] = { "hd0", "hd1", "hd2", "hd3" };

	for (int i = 0; i < 4; i++)
	{
		uint64_t sectors = ata_identify_drive(bases[i], heads[i]);
		if (sectors > 0)
		{
			if (phys_disk_count < 4)
			{
				uint32_t idx = phys_disk_count++;

				ata_private_data[idx].base_port = bases[i];
				ata_private_data[idx].drive_head = heads[i];

				block_device_t* dev = &ata_physical_devices[idx];
				dev->id = idx;
				dev->name[0] = '\0';
				kernel_strcat_n(dev->name, names[i], sizeof(dev->name));
				dev->total_sectors = sectors;
				dev->read_blocks = ata_block_read_adapter;
				dev->write_blocks = ata_block_write_adapter;
				dev->private_data = &ata_private_data[idx];

				block_device_register(dev);

				ata_parse_mbr(dev);
			}
		}
	}
}

void ata_irq_handler(void)
{
	inb(ATA_PRIMARY_BASE + ATA_REG_STATUS);
	inb(ATA_SECONDARY_BASE + ATA_REG_STATUS);
}

static void ata_wait_bsy(uint16_t base)
{
	uint32_t timeout = ATA_WAIT_TIMEOUT;
	while (inb(base + ATA_REG_STATUS) & ATA_STATUS_BSY)
	{
		if (timeout-- == 0)
		{
			serial_write("ATA: wait BSY timeout\r\n");
			return;
		}
	}
}

static void ata_wait_drq(uint16_t base)
{
	uint32_t timeout = ATA_WAIT_TIMEOUT;
	while (!(inb(base + ATA_REG_STATUS) & ATA_STATUS_DRQ))
	{
		if (timeout-- == 0)
		{
			serial_write("ATA: wait DRQ timeout, status=");
			serial_hex(inb(base + ATA_REG_STATUS));
			serial_write("\r\n");
			return;
		}
	}
}

static void ata_send_lba(uint16_t base, uint8_t head, uint32_t lba, uint8_t sector_count)
{
	ata_wait_bsy(base);

	outb(base + ATA_REG_DRIVE_SEL, head | ((lba >> 24) & 0x0F));
	io_wait();

	outb(base + ATA_REG_SECTOR_CNT, sector_count);
	outb(base + ATA_REG_LBA_LOW,  (uint8_t)(lba));
	outb(base + ATA_REG_LBA_MID,  (uint8_t)(lba >> 8));
	outb(base + ATA_REG_LBA_HIGH, (uint8_t)(lba >> 16));
}

int ata_read_sector_raw(uint16_t base_port, uint8_t drive_head, uint32_t lba, uint16_t* buffer)
{
	spin_lock(&ata_lock);

	ata_send_lba(base_port, drive_head, lba, 1);
	outb(base_port + ATA_REG_COMMAND, ATA_CMD_READ);

	ata_wait_bsy(base_port);
	ata_wait_drq(base_port);

	uint8_t status = inb(base_port + ATA_REG_STATUS);
	if (status & ATA_STATUS_ERR)
	{
		serial_write("ATA: Read Error! lba=");
		serial_dec(lba);
		serial_write(" status=");
		serial_hex(status);
		serial_write(" err=");
		serial_hex(inb(base_port + ATA_REG_ERROR));
		serial_write("\r\n");
		spin_unlock(&ata_lock);
		return -1;
	}

	for (int i = 0; i < 256; i++)
	{
		buffer[i] = inw(base_port + ATA_REG_DATA);
	}

	spin_unlock(&ata_lock);
	return 0;
}

int ata_write_sector_raw(uint16_t base_port, uint8_t drive_head, uint32_t lba, const uint16_t* buffer)
{
	spin_lock(&ata_lock);

	ata_send_lba(base_port, drive_head, lba, 1);
	outb(base_port + ATA_REG_COMMAND, ATA_CMD_WRITE);

	ata_wait_bsy(base_port);
	ata_wait_drq(base_port);

	for (int i = 0; i < 256; i++)
	{
		outw(base_port + ATA_REG_DATA, buffer[i]);
	}

	for (uint32_t timeout = ATA_WAIT_TIMEOUT; timeout > 0; timeout--)
	{
		uint8_t status = inb(base_port + ATA_REG_STATUS);
		if (status & ATA_STATUS_ERR)
		{
			serial_write("ATA: Write Error!\r\n");
			spin_unlock(&ata_lock);
			return -1;
		}
		if (!(status & ATA_STATUS_BSY) && !(status & ATA_STATUS_DRQ))
		{
			break;
		}
	}

	spin_unlock(&ata_lock);
	return 0;
}

uint64_t ata_identify_drive(uint16_t base, uint8_t head)
{
	outb(base + ATA_REG_DRIVE_SEL, head);
	io_wait();

	outb(base + ATA_REG_SECTOR_CNT, 0);
	outb(base + ATA_REG_LBA_LOW, 0);
	outb(base + ATA_REG_LBA_MID, 0);
	outb(base + ATA_REG_LBA_HIGH, 0);

	outb(base + ATA_REG_COMMAND, ATA_CMD_IDENTIFY);
	io_wait();

	uint8_t status = inb(base + ATA_REG_STATUS);
	if (status == 0 || status == 0xFF) return 0;

	ata_wait_bsy(base);

	if (inb(base + ATA_REG_STATUS) & ATA_STATUS_ERR) return 0;

	ata_wait_drq(base);
	if (!(inb(base + ATA_REG_STATUS) & ATA_STATUS_DRQ)) return 0;

	uint16_t id_data[256];
	for (int i = 0; i < 256; i++)
	{
		id_data[i] = inw(base + ATA_REG_DATA);
	}

	uint32_t sectors_lba28 = id_data[60] | ((uint32_t)id_data[61] << 16);
	return (uint64_t)sectors_lba28;
}


int ata_block_read_adapter(struct block_device* dev, uint64_t lba, uint32_t count, void* buffer)
{
	ata_private_t* private = (ata_private_t*)dev->private_data;
	uint16_t* ptr = (uint16_t*)buffer;
	for (uint32_t i = 0; i < count; i++)
	{
		if (ata_read_sector_raw(private->base_port, private->drive_head, (uint32_t)(lba + i), ptr + (i * 256)) != 0)
		{
			return -1;
		}
	}
	return 0;
}

int ata_block_write_adapter(struct block_device* dev, uint64_t lba, uint32_t count, void* buffer)
{
	ata_private_t* private = (ata_private_t*)dev->private_data;
	const uint16_t* ptr = (const uint16_t*)buffer;
	for (uint32_t i = 0; i < count; i++)
	{
		if (ata_write_sector_raw(private->base_port, private->drive_head, (uint32_t)(lba + i), ptr + (i * 256)) != 0)
		{
			return -1;
		}
	}
	return 0;
}

int partition_read_adapter(struct block_device* dev, uint64_t lba, uint32_t count, void* buffer)
{
	block_device_t* parent_disk = (block_device_t*)dev->private_data;
	if (parent_disk == NULL || parent_disk->read_blocks == NULL) return -1;

	uint64_t parent_lba = lba + dev->id;
	if (lba + count > dev->total_sectors) return -1;
	if (parent_lba + count > parent_disk->total_sectors) return -1;

	return parent_disk->read_blocks(parent_disk, parent_lba, count, buffer);
}

int partition_write_adapter(struct block_device* dev, uint64_t lba, uint32_t count, void* buffer)
{
	block_device_t* parent_disk = (block_device_t*)dev->private_data;
	if (parent_disk == NULL || parent_disk->write_blocks == NULL) return -1;

	uint64_t parent_lba = lba + dev->id;
	if (lba + count > dev->total_sectors) return -1;
	if (parent_lba + count > parent_disk->total_sectors) return -1;

	return parent_disk->write_blocks(parent_disk, parent_lba, count, buffer);
}

void ata_parse_mbr(block_device_t* phys_dev)
{
	uint16_t sector_buffer[256];
	if (phys_dev->read_blocks(phys_dev, 0, 1, sector_buffer) != 0) return;

	mbr_sector_t* mbr = (mbr_sector_t*)sector_buffer;
	if (mbr->signature != 0xAA55) return;

	for (int i = 0; i < 4; i++)
	{
		mbr_partition_t* part = &mbr->partitions[i];
		if (part->total_sectors == 0) continue;

		if (part->partition_type == 0xEE)
		{
			serial_write("ATA MBR: Protective MBR detected. Switching to GPT parser.\r\n");
			ata_parse_gpt(phys_dev);
			return;
		}

		if (partition_count < MAX_PARTITIONS)
		{
			block_device_t* p_dev = &partition_devices[partition_count++];
			p_dev->id = part->starting_lba;

			p_dev->name[0] = '\0';
			kernel_strcat_n(p_dev->name, phys_dev->name, sizeof(p_dev->name));
			kernel_strcat_n(p_dev->name, "p", sizeof(p_dev->name));

			char part_num_buf[12];
			kernel_itoa(i + 1, part_num_buf, 10);
			kernel_strcat_n(p_dev->name, part_num_buf, sizeof(p_dev->name));

			p_dev->total_sectors = part->total_sectors;
			p_dev->read_blocks = partition_read_adapter;
			p_dev->write_blocks = partition_write_adapter;
			p_dev->private_data = (void*)phys_dev;

			block_device_register(p_dev);
		}
	}
}

void ata_parse_gpt(block_device_t* phys_dev)
{
	uint16_t header_buffer[256];

	serial_write("ATA GPT: Parsing GPT for ");
	serial_write(phys_dev->name);
	serial_write("...\r\n");

	if (phys_dev->read_blocks(phys_dev, 1, 1, header_buffer) != 0)
	{
		serial_write("ATA GPT: Failed to read LBA 1\r\n");
		return;
	}

	gpt_header_t* gpt = (gpt_header_t*)header_buffer;

	if (gpt->signature != 0x5452415020494645ULL)
	{
		serial_write("ATA GPT: Invalid GPT signature. Skipping.\r\n");
		return;
	}

	if (gpt->size_of_partition_entry < 128 || gpt->size_of_partition_entry > 512)
	{
		serial_write("ATA GPT: Invalid partition entry size. Skipping.\r\n");
		return;
	}

	if (gpt->size_of_partition_entry % 8 != 0)
	{
		serial_write("ATA GPT: Unaligned partition entry size. Skipping.\r\n");
		return;
	}

	uint32_t max_entries = 512 / gpt->size_of_partition_entry;
	if (gpt->num_partition_entries == 0 ||
	    gpt->num_partition_entries > 512 ||
	    gpt->partition_entry_lba == 0 ||
	    gpt->partition_entry_lba >= phys_dev->total_sectors)
	{
		serial_write("ATA GPT: Implausible header fields. Skipping.\r\n");
		return;
	}

	uint16_t entry_buffer[256];
	uint32_t entries_per_sector = max_entries;

	uint64_t current_entry_lba = gpt->partition_entry_lba;
	uint32_t processed_entries = 0;

	while (processed_entries < gpt->num_partition_entries)
	{
		if (current_entry_lba >= phys_dev->total_sectors)
		{
			serial_write("ATA GPT: Partition table runs past end of disk. Stopping.\r\n");
			break;
		}

		if (phys_dev->read_blocks(phys_dev, current_entry_lba, 1, entry_buffer) != 0)
		{
			serial_write("ATA GPT: Failed to read partition entry sector\r\n");
			break;
		}

		for (uint32_t i = 0; i < entries_per_sector; i++)
		{
			uintptr_t offset = i * gpt->size_of_partition_entry;
			gpt_entry_t* entry = (gpt_entry_t*)((uintptr_t)entry_buffer + offset);

			const uint8_t* type_guid = (const uint8_t*)&entry->partition_type_guid;
			int type_is_empty = 1;
			for (int b = 0; b < 16; b++)
			{
				if (type_guid[b] != 0)
				{
					type_is_empty = 0;
					break;
				}
			}
			if (type_is_empty)
			{
				processed_entries++;
				continue;
			}

			uint64_t start_lba = entry->starting_lba;
			uint64_t end_lba = entry->ending_lba;

			if (end_lba < start_lba ||
			    start_lba == 0 ||
			    end_lba >= phys_dev->total_sectors)
			{
				serial_write("ATA GPT: Partition entry out of range. Skipping.\r\n");
				processed_entries++;
				continue;
			}

			uint64_t part_sectors = (end_lba - start_lba) + 1;

			serial_write("ATA: Found GPT Partition ");
			serial_dec(partition_count + 1);
			serial_write(": Start LBA: ");
			serial_dec(start_lba);
			serial_write(", Sectors: ");
			serial_dec(part_sectors);
			serial_write("\r\n");

			if (partition_count < MAX_PARTITIONS)
			{
				block_device_t* p_dev = &partition_devices[partition_count++];
				p_dev->id = (uint32_t)start_lba;

				p_dev->name[0] = '\0';
				kernel_strcat_n(p_dev->name, phys_dev->name, sizeof(p_dev->name));
				kernel_strcat_n(p_dev->name, "p", sizeof(p_dev->name));

				char part_num_buf[16];
				kernel_itoa(partition_count, part_num_buf, 10);
				kernel_strcat_n(p_dev->name, part_num_buf, sizeof(p_dev->name));

				p_dev->total_sectors = part_sectors;
				p_dev->read_blocks = partition_read_adapter;
				p_dev->write_blocks = partition_write_adapter;
				p_dev->private_data = (void*)phys_dev;

				block_device_register(p_dev);
			}

			processed_entries++;
			if (processed_entries >= gpt->num_partition_entries) break;
		}

		current_entry_lba++;
	}
}
