#include "ata.h"
#include "io.h"
#include "serial.h"
#include "spinlock.h"
#include "mbr.h"

#define MAX_PARTITIONS 8
#define ATA_WAIT_TIMEOUT 2000000

static volatile thread_t* ata_waiting_thread = NULL;
static volatile uint8_t ata_irq_fired = 0;
static spinlock_t ata_lock = {0};
static block_device_t ata_physical_devices[4];
static ata_private_t ata_private_data[4];
static uint32_t phys_disk_count = 0;
static block_device_t partition_devices[MAX_PARTITIONS];
static uint32_t partition_count = 0;

void ata_init(void)
{
	serial_write("ATA: Scanning IDE Drives...\r\n");
	ata_waiting_thread = NULL;
	ata_irq_fired = 0;

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
				dev->name = names[i];
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
	ata_irq_fired = 1;

	inb(ATA_PRIMARY_BASE + ATA_REG_STATUS);
	inb(ATA_SECONDARY_BASE + ATA_REG_STATUS);

	if (ata_waiting_thread != NULL)
	{
		scheduler_enqueue((thread_t*)ata_waiting_thread);
		ata_waiting_thread = NULL;
	}
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
	ata_irq_fired = 0;

	ata_waiting_thread = current_thread;
	ata_waiting_thread->state = THREAD_BLOCKED;

	ata_send_lba(base_port, drive_head, lba, 1);
	outb(base_port + ATA_REG_COMMAND, ATA_CMD_READ);

	__asm__ volatile("int $0x20");

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
	ata_irq_fired = 0;
	
	ata_waiting_thread = current_thread;
	ata_waiting_thread->state = THREAD_BLOCKED;

	ata_send_lba(base_port, drive_head, lba, 1);
	outb(base_port + ATA_REG_COMMAND, ATA_CMD_WRITE);

	ata_wait_bsy(base_port);
	ata_wait_drq(base_port);

	for (int i = 0; i < 256; i++)
	{
		outw(base_port + ATA_REG_DATA, buffer[i]);
	}

	__asm__ volatile("int $0x20");

	if (inb(base_port + ATA_REG_STATUS) & ATA_STATUS_ERR)
	{
		serial_write("ATA: Write Error!\r\n");
		spin_unlock(&ata_lock);
		return -1;
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
	uint64_t parent_lba = lba + dev->id;
	return parent_disk->read_blocks(parent_disk, parent_lba, count, buffer);
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

		if (partition_count < MAX_PARTITIONS)
		{
			block_device_t* p_dev = &partition_devices[partition_count++];
			p_dev->id = part->starting_lba; 
			
			if (phys_dev->id == 0) p_dev->name = (i == 0) ? "hd0p1" : ((i == 1) ? "hd0p2" : "hd0p3");
			else if (phys_dev->id == 1) p_dev->name = (i == 0) ? "hd1p1" : ((i == 1) ? "hd1p2" : "hd1p3");
			else if (phys_dev->id == 2) p_dev->name = (i == 0) ? "hd2p1" : ((i == 1) ? "hd2p2" : "hd2p3");
			else p_dev->name = (i == 0) ? "hd3p1" : ((i == 1) ? "hd3p2" : "hd3p3");

			p_dev->total_sectors = part->total_sectors;
			p_dev->read_blocks = partition_read_adapter;
			p_dev->write_blocks = phys_dev->write_blocks;
			p_dev->private_data = (void*)phys_dev;

			block_device_register(p_dev);
		}
	}
}
