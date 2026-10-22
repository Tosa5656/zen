#ifndef FAT32_H
#define FAT32_H

#include "vfs.h"
#include "block_device.h"

void fat32_init(void);
int fat32_mount_all(void);
void fat32_list_first(void);

#endif
