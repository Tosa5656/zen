#ifndef MBR_H
#define MBR_H

#include <stdint.h>

typedef struct
{
	uint8_t boot_indicator;
	uint8_t starting_chs[3];
	uint8_t partition_type;
	uint8_t ending_chs[3];
	uint32_t starting_lba;
	uint32_t total_sectors;
} __attribute__((packed)) mbr_partition_t;

typedef struct
{
	uint8_t bootstrap_code[446];
	mbr_partition_t partitions[4];
	uint16_t signature;
} __attribute__((packed)) mbr_sector_t;

#endif
