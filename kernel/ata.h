#pragma once
#ifndef ATA_H
#define ATA_H

#include <stdint.h>
#include "scheduler.h"
#include "block_device.h"

#define ATA_PRIMARY_BASE 0x1F0
#define ATA_SECONDARY_BASE 0x170

#define ATA_REG_DATA 0
#define ATA_REG_ERROR 1
#define ATA_REG_FEATURES 1
#define ATA_REG_SECTOR_CNT 2
#define ATA_REG_LBA_LOW 3
#define ATA_REG_LBA_MID 4
#define ATA_REG_LBA_HIGH 5
#define ATA_REG_DRIVE_SEL 6
#define ATA_REG_COMMAND 7
#define ATA_REG_STATUS 7

#define ATA_CMD_READ 0x20
#define ATA_CMD_WRITE 0x30
#define ATA_CMD_IDENTIFY 0xEC

#define ATA_STATUS_ERR 0x01
#define ATA_STATUS_DRQ 0x08
#define ATA_STATUS_SRV 0x10
#define ATA_STATUS_DF 0x20
#define ATA_STATUS_RDY 0x40
#define ATA_STATUS_BSY 0x80

typedef struct
{
	uint16_t base_port;
	uint8_t drive_head;
} ata_private_t;

void ata_init(void);
void ata_irq_handler(void);
int ata_read_sector_raw(uint16_t base_port, uint8_t drive_head, uint32_t lba, uint16_t* buffer);
int ata_write_sector_raw(uint16_t base_port, uint8_t drive_head, uint32_t lba, const uint16_t* buffer);
uint64_t ata_identify_drive(uint16_t base, uint8_t head);
void ata_parse_mbr(block_device_t* physical_dev);
int ata_block_read_adapter(struct block_device* dev, uint64_t lba, uint32_t count, void* buffer);
int ata_block_write_adapter(struct block_device* dev, uint64_t lba, uint32_t count, void* buffer);

#endif
