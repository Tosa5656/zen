#include "fat32.h"
#include "heap.h"
#include "serial.h"

#include <stddef.h>
#include <string.h>

#define FAT32_SECTOR 512
#define FAT32_NAME_MAX 256
#define FAT32_DIR_CACHE 32

#define FAT32_EOC  0x0FFFFFF8u
#define FAT32_BAD  0x0FFFFFF7u
#define FAT32_MASK 0x0FFFFFFFu

#define FAT32_ATTR_READ_ONLY 0x01
#define FAT32_ATTR_HIDDEN    0x02
#define FAT32_ATTR_SYSTEM    0x04
#define FAT32_ATTR_VOLUME_ID 0x08
#define FAT32_ATTR_DIRECTORY 0x10
#define FAT32_ATTR_ARCHIVE   0x20
#define FAT32_ATTR_LFN       0x0F

#define FAT32_DIR_FREE 0xE5
#define FAT32_DIR_END  0x00

typedef struct
{
	uint8_t  jump[3];
	char     oem[8];
	uint16_t bytes_per_sector;
	uint8_t  sectors_per_cluster;
	uint16_t reserved_sectors;
	uint8_t  fat_count;
	uint16_t root_entries;
	uint16_t total_sectors16;
	uint8_t  media;
	uint16_t fat_size16;
	uint16_t sectors_per_track;
	uint16_t head_count;
	uint32_t hidden_sectors;
	uint32_t total_sectors32;
	uint32_t fat_size32;
	uint16_t ext_flags;
	uint16_t fs_version;
	uint32_t root_cluster;
	uint16_t fsinfo_sector;
	uint16_t backup_boot_sector;
	uint8_t  reserved[12];
	uint8_t  drive_number;
	uint8_t  reserved1;
	uint8_t  boot_signature;
	uint32_t volume_id;
	char     volume_label[11];
	char     fs_type[8];
} __attribute__((packed)) fat32_bpb_t;

typedef struct
{
	char     name[11];
	uint8_t  attr;
	uint8_t  nt_reserved;
	uint8_t  creation_tenths;
	uint16_t creation_time;
	uint16_t creation_date;
	uint16_t access_date;
	uint16_t cluster_high;
	uint16_t write_time;
	uint16_t write_date;
	uint16_t cluster_low;
	uint32_t size;
} __attribute__((packed)) fat32_dirent_t;

typedef struct
{
	uint8_t  sequence;
	uint16_t name1[5];
	uint8_t  attr;
	uint8_t  type;
	uint8_t  checksum;
	uint16_t name2[6];
	uint16_t cluster_low;
	uint16_t name3[2];
} __attribute__((packed)) fat32_lfn_t;


typedef struct fat32_mount
{
	block_device_t* dev;
	char label[12];

	uint32_t bytes_per_sector;
	uint32_t sectors_per_cluster;
	uint32_t cluster_bytes;
	uint32_t cluster_count;

	uint64_t fat_start;
	uint64_t fat_size;
	uint64_t data_start;
	uint32_t fat_count;
	uint32_t root_cluster;
	uint64_t total_sectors;

	uint32_t* fat;
	uint32_t fat_entries;
	uint32_t alloc_hint;

	vfs_node_t* root;
	vfs_node_t* dir_cache[FAT32_DIR_CACHE];
	uint32_t dir_cache_count;
} fat32_mount_t;

typedef struct fat32_file
{
	fat32_mount_t* mount;
	uint32_t first_cluster;
	uint32_t dir_cluster;
	uint32_t size;
	uint8_t  attr;
} fat32_file_t;

typedef struct
{
	uint16_t units[FAT32_NAME_MAX];
	uint32_t count;
	uint32_t next_sequence;
	int active;
} fat32_lfn_state_t;

static fs_driver_t fat32_driver;

#define FAT32_MAX_VOLUMES 8
static fat32_mount_t* fat32_volumes[FAT32_MAX_VOLUMES];
static uint32_t fat32_volume_count = 0;

/* TODO: Remove after add libc */
static int fat32_strieq(const char* a, const char* b)
{
	while (*a != '\0' && *b != '\0')
	{
		char ca = (*a >= 'A' && *a <= 'Z') ? (char)(*a + 32) : *a;
		char cb = (*b >= 'A' && *b <= 'Z') ? (char)(*b + 32) : *b;

		if (ca != cb)
		{
			return 0;
		}
		a++;
		b++;
	}

	return (*a == '\0' && *b == '\0');
}

static void fat32_memzero(void* dst, uint64_t size)
{
	uint8_t* p = (uint8_t*)dst;
	for (uint64_t i = 0; i < size; i++)
	{
		p[i] = 0;
	}
}

/* ---- */

static int fat32_sector_read(fat32_mount_t* m, uint64_t lba, void* buffer)
{
	if (lba >= m->total_sectors || m->dev->read_blocks == NULL)
	{
		return -1;
	}

	return m->dev->read_blocks(m->dev, lba, 1, buffer);
}

static int fat32_sector_write(fat32_mount_t* m, uint64_t lba, const void* buffer)
{
	if (lba >= m->total_sectors || m->dev->write_blocks == NULL)
	{
		return -1;
	}

	return m->dev->write_blocks(m->dev, lba, 1, (void*)buffer);
}

static uint64_t fat32_cluster_lba(fat32_mount_t* m, uint32_t cluster)
{
	return m->data_start + (uint64_t)(cluster - 2) * m->sectors_per_cluster;
}

static int fat32_cluster_read(fat32_mount_t* m, uint32_t cluster, uint64_t offset, uint32_t size, void* buffer)
{
	if (cluster < 2 || cluster >= m->cluster_count + 2 || offset + size > m->cluster_bytes)
	{
		return -1;
	}

	uint64_t sector = fat32_cluster_lba(m, cluster) + offset / m->bytes_per_sector;
	uint32_t in_sector = (uint32_t)(offset % m->bytes_per_sector);
	uint8_t* out = (uint8_t*)buffer;
	uint32_t done = 0;

	while (done < size)
	{
		uint32_t chunk = m->bytes_per_sector - in_sector;
		if (chunk > size - done)
		{
			chunk = size - done;
		}

		if (in_sector == 0 && chunk == m->bytes_per_sector)
		{
			if (fat32_sector_read(m, sector, out + done) != 0)
			{
				return -1;
			}
		}
		else
		{
			uint8_t scratch[FAT32_SECTOR];

			if (fat32_sector_read(m, sector, scratch) != 0)
			{
				return -1;
			}
			for (uint32_t i = 0; i < chunk; i++)
			{
				out[done + i] = scratch[in_sector + i];
			}
		}

		done += chunk;
		sector++;
		in_sector = 0;
	}

	return (int)size;
}

static int fat32_cluster_write(fat32_mount_t* m, uint32_t cluster, uint64_t offset, uint32_t size, const void* buffer)
{
	if (cluster < 2 || cluster >= m->cluster_count + 2 || offset + size > m->cluster_bytes)
	{
		return -1;
	}

	uint64_t sector = fat32_cluster_lba(m, cluster) + offset / m->bytes_per_sector;
	uint32_t in_sector = (uint32_t)(offset % m->bytes_per_sector);
	const uint8_t* in = (const uint8_t*)buffer;
	uint32_t done = 0;

	while (done < size)
	{
		uint32_t chunk = m->bytes_per_sector - in_sector;
		if (chunk > size - done)
		{
			chunk = size - done;
		}

		if (in_sector == 0 && chunk == m->bytes_per_sector)
		{
			if (fat32_sector_write(m, sector, in + done) != 0)
			{
				return -1;
			}
		}
		else
		{
			uint8_t scratch[FAT32_SECTOR];

			if (fat32_sector_read(m, sector, scratch) != 0)
			{
				return -1;
			}
			for (uint32_t i = 0; i < chunk; i++)
			{
				scratch[in_sector + i] = in[done + i];
			}
			if (fat32_sector_write(m, sector, scratch) != 0)
			{
				return -1;
			}
		}

		done += chunk;
		sector++;
		in_sector = 0;
	}

	return (int)size;
}

static uint32_t fat32_next_cluster(fat32_mount_t* m, uint32_t cluster)
{
	if (cluster < 2 || cluster >= m->fat_entries)
	{
		return FAT32_EOC;
	}

	return m->fat[cluster] & FAT32_MASK;
}

static int fat32_set_next(fat32_mount_t* m, uint32_t cluster, uint32_t value)
{
	if (cluster < 2 || cluster >= m->fat_entries)
	{
		return -1;
	}

	uint32_t entry = (m->fat[cluster] & ~FAT32_MASK) | (value & FAT32_MASK);
	m->fat[cluster] = entry;

	uint64_t byte_offset = (uint64_t)cluster * 4;
	uint32_t in_sector = (uint32_t)(byte_offset % m->bytes_per_sector);
	uint64_t sector_offset = byte_offset / m->bytes_per_sector;

	uint8_t scratch[FAT32_SECTOR];

	for (uint32_t copy = 0; copy < m->fat_count; copy++)
	{
		uint64_t lba = m->fat_start + (uint64_t)copy * m->fat_size + sector_offset;

		if (fat32_sector_read(m, lba, scratch) != 0)
		{
			return -1;
		}

		scratch[in_sector + 0] = (uint8_t)(entry);
		scratch[in_sector + 1] = (uint8_t)(entry >> 8);
		scratch[in_sector + 2] = (uint8_t)(entry >> 16);
		scratch[in_sector + 3] = (uint8_t)(entry >> 24);

		if (fat32_sector_write(m, lba, scratch) != 0)
		{
			return -1;
		}
	}

	return 0;
}

static uint32_t fat32_alloc_cluster(fat32_mount_t* m)
{
	uint32_t start = (m->alloc_hint >= 2 && m->alloc_hint < m->fat_entries) ? m->alloc_hint : 2;

	for (uint32_t pass = 0; pass < 2; pass++)
	{
		uint32_t from = (pass == 0) ? start : 2;
		uint32_t to = (pass == 0) ? m->fat_entries : start;

		for (uint32_t cluster = from; cluster < to; cluster++)
		{
			if ((m->fat[cluster] & FAT32_MASK) == 0)
			{
				if (fat32_set_next(m, cluster, FAT32_EOC) != 0)
				{
					return 0;
				}

				m->alloc_hint = cluster + 1;
				return cluster;
			}
		}
	}

	return 0;
}

static uint32_t fat32_chain_append(fat32_mount_t* m, uint32_t cluster)
{
	uint32_t current = cluster;

	if (current < 2)
	{
		return 0;
	}

	for (uint32_t guard = 0; guard < m->cluster_count; guard++)
	{
		uint32_t next = fat32_next_cluster(m, current);

		if (next >= FAT32_EOC || next < 2)
		{
			uint32_t fresh = fat32_alloc_cluster(m);
			if (fresh == 0)
			{
				return 0;
			}

			if (fat32_set_next(m, current, fresh) != 0)
			{
				fat32_set_next(m, fresh, 0);
				return 0;
			}

			return fresh;
		}

		current = next;
	}

	return 0;
}

static void fat32_chain_free(fat32_mount_t* m, uint32_t cluster)
{
	for (uint32_t guard = 0; guard < m->cluster_count && cluster >= 2 && cluster < FAT32_BAD; guard++)
	{
		uint32_t next = fat32_next_cluster(m, cluster);

		fat32_set_next(m, cluster, 0);

		if (next >= FAT32_EOC || next < 2)
		{
			break;
		}

		cluster = next;
	}
}

static int fat32_load_fat(fat32_mount_t* m)
{
	m->fat_entries = m->cluster_count + 2;
	m->fat = kcalloc(m->fat_entries, sizeof(uint32_t));

	if (m->fat == NULL)
	{
		return -1;
	}

	uint8_t sector[FAT32_SECTOR];
	uint64_t entries_per_sector = m->bytes_per_sector / 4;
	uint64_t loaded = 0;

	for (uint64_t s = 0; s < m->fat_size && loaded < m->fat_entries; s++)
	{
		if (fat32_sector_read(m, m->fat_start + s, sector) != 0)
		{
			return -1;
		}

		uint32_t* values = (uint32_t*)sector;

		for (uint64_t i = 0; i < entries_per_sector && loaded < m->fat_entries; i++)
		{
			m->fat[loaded++] = values[i];
		}
	}

	return 0;
}

static void fat32_shortname_to_string(const fat32_dirent_t* entry, char* out)
{
	uint32_t o = 0;

	out[o++] = ((uint8_t)entry->name[0] == 0x05) ? (char)0xE5 : entry->name[0];

	for (int i = 1; i < 8 && entry->name[i] != ' '; i++)
	{
		out[o++] = entry->name[i];
	}

	int ext = 8;
	while (ext < 11 && entry->name[ext] == ' ')
	{
		ext++;
	}

	if (ext < 11)
	{
		out[o++] = '.';
		for (int i = ext; i < 11 && entry->name[i] != ' '; i++)
		{
			out[o++] = entry->name[i];
		}
	}

	out[o] = '\0';
}

static void fat32_shortname_encode(const char* name, char* out)
{
	for (int i = 0; i < 11; i++)
	{
		out[i] = ' ';
	}

	uint32_t i = 0;
	uint32_t o = 0;

	for (; name[i] != '\0' && name[i] != '.' && o < 8; i++)
	{
		char c = name[i];
		if (c >= 'a' && c <= 'z')
		{
			c = (char)(c - 32);
		}
		out[o++] = c;
	}

	while (name[i] != '\0' && name[i] != '.')
	{
		i++;
	}

	if (name[i] == '.')
	{
		i++;
		o = 8;
		for (; name[i] != '\0' && o < 11; i++)
		{
			char c = name[i];
			if (c >= 'a' && c <= 'z')
			{
				c = (char)(c - 32);
			}
			out[o++] = c;
		}
	}
}

static int fat32_shortname_valid(const char* name)
{
	uint32_t base = 0;
	uint32_t ext = 0;
	int seen_dot = 0;

	for (uint32_t i = 0; name[i] != '\0'; i++)
	{
		char c = name[i];

		if (c == '.')
		{
			if (seen_dot)
			{
				return 0;
			}
			seen_dot = 1;
			continue;
		}

		if (c == ' ' || c == '/' || c == '\\' || c == '*' || c == '?' ||
			c == ':' || c == '|' || c == '"' || c == '<' || c == '>')
		{
			return 0;
		}

		if (seen_dot)
		{
			ext++;
		}
		else
		{
			base++;
		}
	}

	return base > 0 && base <= 8 && ext <= 3;
}

static uint8_t fat32_shortname_checksum(const char* short_name)
{
	uint8_t sum = 0;

	for (int i = 0; i < 11; i++)
	{
		sum = (uint8_t)(((sum & 1) ? 0x80 : 0) + (sum >> 1) + (uint8_t)short_name[i]);
	}

	return sum;
}

static void fat32_shortname_generate(const char* name, char* out)
{
	char alias[16];
	uint32_t o = 0;
	uint32_t i = 0;

	for (; name[i] != '\0' && name[i] != '.' && o < 6; i++)
	{
		char c = name[i];
		if (c >= 'a' && c <= 'z')
		{
			c = (char)(c - 32);
		}
		if (c != ' ' && c != '.' && c != '+' && c != ',' && c != ';' && c != '=')
		{
			alias[o++] = c;
		}
	}

	alias[o++] = '~';
	alias[o++] = '1';

	while (name[i] != '\0' && name[i] != '.')
	{
		i++;
	}

	if (name[i] == '.')
	{
		i++;
		while (name[i] != '\0' && o < 12)
		{
			char c = name[i];
			if (c >= 'a' && c <= 'z')
			{
				c = (char)(c - 32);
			}
			alias[o++] = c;
			i++;
		}
	}

	alias[o] = '\0';
	fat32_shortname_encode(alias, out);
}

static uint32_t fat32_cluster_of(const fat32_dirent_t* entry)
{
	return ((uint32_t)entry->cluster_high << 16) | entry->cluster_low;
}

static void fat32_lfn_reset(fat32_lfn_state_t* state)
{
	state->count = 0;
	state->next_sequence = 0;
	state->active = 0;
}

static void fat32_lfn_apply(fat32_lfn_state_t* state, const fat32_lfn_t* lfn)
{
	uint32_t sequence = lfn->sequence & 0x1F;

	if (sequence == 0 || sequence > 20)
	{
		fat32_lfn_reset(state);
		return;
	}

	if (lfn->sequence & 0x40)
	{
		fat32_memzero(state->units, sizeof(state->units));
		state->count = 0;
		state->next_sequence = sequence;
		state->active = 1;
	}

	if (!state->active || sequence != state->next_sequence)
	{
		fat32_lfn_reset(state);
		return;
	}

	uint32_t base = (sequence - 1) * 13;
	uint16_t chunk[13];

	for (int i = 0; i < 5; i++) chunk[i] = lfn->name1[i];
	for (int i = 0; i < 6; i++) chunk[5 + i] = lfn->name2[i];
	for (int i = 0; i < 2; i++) chunk[11 + i] = lfn->name3[i];

	for (uint32_t i = 0; i < 13 && base + i < FAT32_NAME_MAX; i++)
	{
		state->units[base + i] = chunk[i];
	}

	if (base + 13 > state->count)
	{
		state->count = base + 13;
	}

	if (state->next_sequence > 1)
	{
		state->next_sequence--;
	}
}

static void fat32_lfn_to_ascii(const fat32_lfn_state_t* state, char* out, uint32_t out_size)
{
	uint32_t o = 0;

	for (uint32_t i = 0; i < state->count && o + 1 < out_size; i++)
	{
		uint16_t unit = state->units[i];

		if (unit == 0x0000 || unit == 0xFFFF)
		{
			break;
		}

		out[o++] = (unit < 0x80) ? (char)unit : '?';
	}

	out[o] = '\0';
}

typedef struct
{
	uint32_t cluster;
	uint32_t offset;
} fat32_slot_t;

static int fat32_slot_write(fat32_mount_t* m, fat32_slot_t slot, const fat32_dirent_t* entry)
{
	uint8_t sector[FAT32_SECTOR];
	uint64_t lba = fat32_cluster_lba(m, slot.cluster) + slot.offset / m->bytes_per_sector;
	uint32_t in_sector = slot.offset % m->bytes_per_sector;

	if (fat32_sector_read(m, lba, sector) != 0)
	{
		return -1;
	}

	*(fat32_dirent_t*)(sector + in_sector) = *entry;

	return fat32_sector_write(m, lba, sector);
}

static int fat32_dir_next_cluster(fat32_mount_t* m, uint32_t* cluster)
{
	uint32_t next = fat32_next_cluster(m, *cluster);

	if (next >= FAT32_EOC || next < 2)
	{
		return -1;
	}

	*cluster = next;
	return 0;
}

static int fat32_dir_lookup(fat32_mount_t* m, uint32_t dir_cluster, const char* name, fat32_dirent_t* out_entry, fat32_slot_t* out_slot)
{
	fat32_lfn_state_t lfn;
	fat32_lfn_reset(&lfn);

	uint32_t cluster = dir_cluster;

	for (uint32_t guard = 0; guard < m->cluster_count && cluster >= 2 && cluster < FAT32_EOC; guard++)
	{
		for (uint32_t s = 0; s < m->sectors_per_cluster; s++)
		{
			uint8_t sector[FAT32_SECTOR];
			uint64_t lba = fat32_cluster_lba(m, cluster) + s;

			if (fat32_sector_read(m, lba, sector) != 0)
			{
				return -1;
			}

			for (uint32_t offset = 0; offset + 32 <= m->bytes_per_sector; offset += 32)
			{
				fat32_dirent_t* entry = (fat32_dirent_t*)(sector + offset);

				if ((uint8_t)entry->name[0] == FAT32_DIR_END)
				{
					return -1;
				}

				if ((uint8_t)entry->name[0] == FAT32_DIR_FREE)
				{
					fat32_lfn_reset(&lfn);
					continue;
				}

				if (entry->attr == FAT32_ATTR_LFN)
				{
					fat32_lfn_apply(&lfn, (const fat32_lfn_t*)entry);
					continue;
				}

				if (entry->attr & FAT32_ATTR_VOLUME_ID)
				{
					fat32_lfn_reset(&lfn);
					continue;
				}

				char short_name[16];
				fat32_shortname_to_string(entry, short_name);

				char long_name[FAT32_NAME_MAX];
				fat32_lfn_to_ascii(&lfn, long_name, sizeof(long_name));

				if (fat32_strieq(short_name, name) ||
					(long_name[0] != '\0' && fat32_strieq(long_name, name)))
				{
					if (out_entry != NULL)
					{
						*out_entry = *entry;
					}
					if (out_slot != NULL)
					{
						out_slot->cluster = cluster;
						out_slot->offset = s * m->bytes_per_sector + offset;
					}

					return 0;
				}

				fat32_lfn_reset(&lfn);
			}
		}

		if (fat32_dir_next_cluster(m, &cluster) != 0)
		{
			break;
		}
	}

	return -1;
}

static uint32_t fat32_dir_count(fat32_mount_t* m, uint32_t dir_cluster)
{
	uint32_t cluster = dir_cluster;
	uint32_t count = 0;

	for (uint32_t guard = 0; guard < m->cluster_count && cluster >= 2 && cluster < FAT32_EOC; guard++)
	{
		for (uint32_t s = 0; s < m->sectors_per_cluster; s++)
		{
			uint8_t sector[FAT32_SECTOR];
			uint64_t lba = fat32_cluster_lba(m, cluster) + s;

			if (fat32_sector_read(m, lba, sector) != 0)
			{
				return count;
			}

			for (uint32_t offset = 0; offset + 32 <= m->bytes_per_sector; offset += 32)
			{
				fat32_dirent_t* entry = (fat32_dirent_t*)(sector + offset);

				if ((uint8_t)entry->name[0] == FAT32_DIR_END)
				{
					return count;
				}

				if ((uint8_t)entry->name[0] == FAT32_DIR_FREE ||
					entry->attr == FAT32_ATTR_LFN ||
					(entry->attr & FAT32_ATTR_VOLUME_ID))
				{
					continue;
				}

				if (entry->name[0] == '.' &&
					(entry->name[1] == ' ' || entry->name[1] == '.'))
				{
					continue;
				}

				count++;
			}
		}

		if (fat32_dir_next_cluster(m, &cluster) != 0)
		{
			break;
		}
	}

	return count;
}

static int fat32_dir_nth(fat32_mount_t* m, uint32_t dir_cluster, uint32_t wanted,
						 fat32_dirent_t* out_entry, char* out_name, uint32_t out_name_size)
{
	fat32_lfn_state_t lfn;
	fat32_lfn_reset(&lfn);

	uint32_t cluster = dir_cluster;
	uint32_t index = 0;

	for (uint32_t guard = 0; guard < m->cluster_count && cluster >= 2 && cluster < FAT32_EOC; guard++)
	{
		for (uint32_t s = 0; s < m->sectors_per_cluster; s++)
		{
			uint8_t sector[FAT32_SECTOR];
			uint64_t lba = fat32_cluster_lba(m, cluster) + s;

			if (fat32_sector_read(m, lba, sector) != 0)
			{
				return -1;
			}

			for (uint32_t offset = 0; offset + 32 <= m->bytes_per_sector; offset += 32)
			{
				fat32_dirent_t* entry = (fat32_dirent_t*)(sector + offset);

				if ((uint8_t)entry->name[0] == FAT32_DIR_END)
				{
					return -1;
				}

				if ((uint8_t)entry->name[0] == FAT32_DIR_FREE)
				{
					fat32_lfn_reset(&lfn);
					continue;
				}

				if (entry->attr == FAT32_ATTR_LFN)
				{
					fat32_lfn_apply(&lfn, (const fat32_lfn_t*)entry);
					continue;
				}

				if (entry->attr & FAT32_ATTR_VOLUME_ID)
				{
					fat32_lfn_reset(&lfn);
					continue;
				}

				if (entry->name[0] == '.' &&
					(entry->name[1] == ' ' || entry->name[1] == '.'))
				{
					fat32_lfn_reset(&lfn);
					continue;
				}

				if (index == wanted)
				{
					if (out_entry != NULL)
					{
						*out_entry = *entry;
					}
					if (out_name != NULL)
					{
						if (lfn.count > 0)
						{
							fat32_lfn_to_ascii(&lfn, out_name, out_name_size);
						}
						else
						{
							char short_name[16];
							fat32_shortname_to_string(entry, short_name);
							strncpy(out_name, short_name, out_name_size - 1);
							out_name[out_name_size - 1] = '\0';
						}
					}

					return 0;
				}

				index++;
				fat32_lfn_reset(&lfn);
			}
		}

		if (fat32_dir_next_cluster(m, &cluster) != 0)
		{
			break;
		}
	}

	return -1;
}

static void fat32_dir_free_lfn_before(fat32_mount_t* m, fat32_slot_t slot, uint8_t checksum)
{
	while (slot.offset >= 32)
	{
		slot.offset -= 32;

		uint8_t sector[FAT32_SECTOR];
		uint64_t lba = fat32_cluster_lba(m, slot.cluster) + slot.offset / m->bytes_per_sector;
		uint32_t in_sector = slot.offset % m->bytes_per_sector;

		if (fat32_sector_read(m, lba, sector) != 0)
		{
			return;
		}

		fat32_dirent_t* entry = (fat32_dirent_t*)(sector + in_sector);

		if (entry->attr != FAT32_ATTR_LFN || ((fat32_lfn_t*)entry)->checksum != checksum)
		{
			return;
		}

		sector[in_sector] = FAT32_DIR_FREE;

		if (fat32_sector_write(m, lba, sector) != 0)
		{
			return;
		}
	}
}

static int fat32_dir_alloc_slots(fat32_mount_t* m, uint32_t dir_cluster, uint32_t count, fat32_slot_t* out_start)
{
	if (count == 0 || count > m->cluster_bytes / 32)
	{
		return -1;
	}

	uint32_t cluster = dir_cluster;
	uint32_t run = 0;
	fat32_slot_t start = { 0, 0 };
	uint32_t last_cluster = dir_cluster;

	for (uint32_t guard = 0; guard < m->cluster_count && cluster >= 2 && cluster < FAT32_EOC; guard++)
	{
		run = 0;

		for (uint32_t s = 0; s < m->sectors_per_cluster; s++)
		{
			uint8_t sector[FAT32_SECTOR];
			uint64_t lba = fat32_cluster_lba(m, cluster) + s;

			if (fat32_sector_read(m, lba, sector) != 0)
			{
				return -1;
			}

			for (uint32_t offset = 0; offset + 32 <= m->bytes_per_sector; offset += 32)
			{
				fat32_dirent_t* entry = (fat32_dirent_t*)(sector + offset);
				int free_slot = ((uint8_t)entry->name[0] == FAT32_DIR_END || (uint8_t)entry->name[0] == FAT32_DIR_FREE);

				if (!free_slot)
				{
					run = 0;
					continue;
				}

				if (run == 0)
				{
					start.cluster = cluster;
					start.offset = s * m->bytes_per_sector + offset;
				}

				if (++run == count)
				{
					*out_start = start;
					return 0;
				}
			}
		}

		last_cluster = cluster;

		if (fat32_dir_next_cluster(m, &cluster) != 0)
		{
			break;
		}
	}

	uint32_t fresh = fat32_chain_append(m, last_cluster);
	if (fresh == 0)
	{
		return -1;
	}

	uint8_t blank[FAT32_SECTOR];
	fat32_memzero(blank, sizeof(blank));

	for (uint32_t s = 0; s < m->sectors_per_cluster; s++)
	{
		if (fat32_sector_write(m, fat32_cluster_lba(m, fresh) + s, blank) != 0)
		{
			return -1;
		}
	}

	out_start->cluster = fresh;
	out_start->offset = 0;

	return (count <= m->cluster_bytes / 32) ? 0 : -1;
}

static int fat32_dir_shortname_taken(fat32_mount_t* m, uint32_t dir_cluster, const char* short_name)
{
	uint32_t cluster = dir_cluster;

	for (uint32_t guard = 0; guard < m->cluster_count && cluster >= 2 && cluster < FAT32_EOC; guard++)
	{
		for (uint32_t s = 0; s < m->sectors_per_cluster; s++)
		{
			uint8_t sector[FAT32_SECTOR];
			uint64_t lba = fat32_cluster_lba(m, cluster) + s;

			if (fat32_sector_read(m, lba, sector) != 0)
			{
				return 0;
			}

			for (uint32_t offset = 0; offset + 32 <= m->bytes_per_sector; offset += 32)
			{
				fat32_dirent_t* entry = (fat32_dirent_t*)(sector + offset);

				if ((uint8_t)entry->name[0] == FAT32_DIR_END)
				{
					return 0;
				}

				if ((uint8_t)entry->name[0] == FAT32_DIR_FREE || entry->attr == FAT32_ATTR_LFN)
				{
					continue;
				}

				int same = 1;
				for (int i = 0; i < 11; i++)
				{
					if (entry->name[i] != short_name[i])
					{
						same = 0;
						break;
					}
				}

				if (same)
				{
					return 1;
				}
			}
		}

		if (fat32_dir_next_cluster(m, &cluster) != 0)
		{
			break;
		}
	}

	return 0;
}

static int fat32_dir_add(fat32_mount_t* m, uint32_t dir_cluster, const char* name,
						 uint8_t attr, uint32_t first_cluster, uint32_t size)
{
	char short_name[11];
	int has_long = !fat32_shortname_valid(name);

	if (has_long)
	{
		fat32_shortname_generate(name, short_name);

		for (uint32_t attempt = 1; attempt <= 99; attempt++)
		{
			if (!fat32_dir_shortname_taken(m, dir_cluster, short_name))
			{
				break;
			}

			if (attempt == 99)
			{
				return -1;
			}

			short_name[6] = (char)('0' + (attempt + 1) / 10);
			short_name[7] = (char)('0' + (attempt + 1) % 10);

			if ((attempt + 1) < 10)
			{
				short_name[6] = '~';
				short_name[7] = (char)('0' + (attempt + 1));
			}
		}
	}
	else
	{
		fat32_shortname_encode(name, short_name);

		if (fat32_dir_shortname_taken(m, dir_cluster, short_name))
		{
			return -1;
		}
	}

	uint32_t name_len = 0;
	while (name[name_len] != '\0' && name_len < 255)
	{
		name_len++;
	}

	uint32_t lfn_count = has_long ? (name_len + 12) / 13 : 0;

	fat32_slot_t start;
	if (fat32_dir_alloc_slots(m, dir_cluster, lfn_count + 1, &start) != 0)
	{
		return -1;
	}

	uint8_t checksum = fat32_shortname_checksum(short_name);
	uint32_t slot_index = 0;

	for (uint32_t sequence = lfn_count; sequence >= 1; sequence--)
	{
		fat32_dirent_t raw;
		fat32_memzero(&raw, sizeof(raw));

		fat32_lfn_t* lfn = (fat32_lfn_t*)&raw;
		lfn->sequence = (uint8_t)(sequence | ((sequence == lfn_count) ? 0x40 : 0));
		lfn->attr = FAT32_ATTR_LFN;
		lfn->type = 0;
		lfn->checksum = checksum;
		lfn->cluster_low = 0;

		uint32_t base = (sequence - 1) * 13;

		for (uint32_t i = 0; i < 13; i++)
		{
			uint16_t unit;

			if (base + i < name_len)
			{
				unit = (uint16_t)(uint8_t)name[base + i];
			}
			else if (base + i == name_len)
			{
				unit = 0x0000;
			}
			else
			{
				unit = 0xFFFF;
			}

			if (i < 5)
			{
				lfn->name1[i] = unit;
			}
			else if (i < 11)
			{
				lfn->name2[i - 5] = unit;
			}
			else
			{
				lfn->name3[i - 11] = unit;
			}
		}

		fat32_slot_t slot = start;
		slot.offset += slot_index * 32;

		if (slot.offset >= m->cluster_bytes)
		{
			return -1;
		}

		if (fat32_slot_write(m, slot, &raw) != 0)
		{
			return -1;
		}

		slot_index++;
	}

	fat32_dirent_t entry;
	fat32_memzero(&entry, sizeof(entry));

	for (int i = 0; i < 11; i++)
	{
		entry.name[i] = short_name[i];
	}

	entry.attr = attr;
	entry.cluster_high = (uint16_t)(first_cluster >> 16);
	entry.cluster_low = (uint16_t)(first_cluster & 0xFFFF);
	entry.size = size;

	fat32_slot_t slot = start;
	slot.offset += slot_index * 32;

	if (slot.offset >= m->cluster_bytes)
	{
		return -1;
	}

	return fat32_slot_write(m, slot, &entry);
}

static int fat32_dir_update_entry(fat32_mount_t* m, uint32_t dir_cluster, const char* name, uint32_t first_cluster, uint32_t size)
{
	fat32_dirent_t entry;
	fat32_slot_t slot;

	if (fat32_dir_lookup(m, dir_cluster, name, &entry, &slot) != 0)
	{
		return -1;
	}

	entry.cluster_high = (uint16_t)(first_cluster >> 16);
	entry.cluster_low = (uint16_t)(first_cluster & 0xFFFF);
	entry.size = size;

	return fat32_slot_write(m, slot, &entry);
}

static vfs_operations_t fat32_file_operations;
static vfs_operations_t fat32_dir_operations;

static fat32_file_t* fat32_file_from(vfs_node_t* node)
{
	fat32_file_t* file = (fat32_file_t*)node->private_data;

	if (file == NULL || file->mount == NULL)
	{
		return NULL;
	}

	return file;
}

static vfs_node_t* fat32_cache_find(fat32_mount_t* m, uint32_t dir_cluster, uint32_t first_cluster, const char* name)
{
	for (uint32_t i = 0; i < m->dir_cache_count; i++)
	{
		fat32_file_t* file = (fat32_file_t*)m->dir_cache[i]->private_data;

		if (file == NULL || file->dir_cluster != dir_cluster)
		{
			continue;
		}

		if (fat32_strieq(m->dir_cache[i]->name, name) || file->first_cluster == first_cluster)
		{
			return m->dir_cache[i];
		}
	}

	return NULL;
}

static void fat32_cache_add(fat32_mount_t* m, vfs_node_t* node)
{
	if (m->dir_cache_count < FAT32_DIR_CACHE)
	{
		m->dir_cache[m->dir_cache_count++] = node;
		node->refs = 1;
	}
}

static vfs_node_t* fat32_node_create(fat32_mount_t* m, const char* name, uint32_t first_cluster, uint32_t dir_cluster, uint32_t size, uint8_t attr)
{
	vfs_node_t* cached = fat32_cache_find(m, dir_cluster, first_cluster, name);
	if (cached != NULL)
	{
		cached->size = size;
		return cached;
	}

	vfs_node_t* node = kcalloc(1, sizeof(vfs_node_t));
	fat32_file_t* file = kcalloc(1, sizeof(fat32_file_t));

	if (node == NULL || file == NULL)
	{
		kfree(node);
		kfree(file);
		return NULL;
	}

	strncpy(node->name, name, sizeof(node->name) - 1);
	node->name[sizeof(node->name) - 1] = '\0';

	file->mount = m;
	file->first_cluster = first_cluster;
	file->dir_cluster = dir_cluster;
	file->size = size;
	file->attr = attr;

	int directory = (attr & FAT32_ATTR_DIRECTORY) != 0;

	node->type = directory ? VFS_DIRECTORY : VFS_FILE;
	node->size = size;
	node->inode_num = first_cluster;
	node->refs = 0;
	node->operations = directory ? &fat32_dir_operations : &fat32_file_operations;
	node->private_data = file;
	node->canonical = node;

	fat32_cache_add(m, node);

	return node;
}

static int fat32_read(vfs_node_t* node, uint64_t offset, uint32_t size, void* buffer)
{
	fat32_file_t* file = fat32_file_from(node);
	if (file == NULL || (file->attr & FAT32_ATTR_DIRECTORY))
	{
		return -1;
	}

	if (size == 0 || offset >= file->size)
	{
		return 0;
	}

	fat32_mount_t* m = file->mount;
	uint64_t available = file->size - offset;
	uint32_t wanted = (size < available) ? size : (uint32_t)available;

	uint32_t cluster = file->first_cluster;
	if (cluster < 2)
	{
		return 0;
	}

	uint32_t skip = (uint32_t)(offset / m->cluster_bytes);
	for (uint32_t i = 0; i < skip; i++)
	{
		cluster = fat32_next_cluster(m, cluster);
		if (cluster < 2 || cluster >= FAT32_EOC)
		{
			return 0;
		}
	}

	uint64_t in_cluster = offset % m->cluster_bytes;
	uint8_t* out = (uint8_t*)buffer;
	uint32_t done = 0;

	while (done < wanted)
	{
		uint32_t chunk = m->cluster_bytes - (uint32_t)in_cluster;
		if (chunk > wanted - done)
		{
			chunk = wanted - done;
		}

		if (fat32_cluster_read(m, cluster, in_cluster, chunk, out + done) != (int)chunk)
		{
			return -1;
		}

		done += chunk;
		in_cluster = 0;

		if (done < wanted)
		{
			cluster = fat32_next_cluster(m, cluster);
			if (cluster < 2 || cluster >= FAT32_EOC)
			{
				break;
			}
		}
	}

	return (int)done;
}

static int fat32_write(vfs_node_t* node, uint64_t offset, uint32_t size, const void* buffer)
{
	fat32_file_t* file = fat32_file_from(node);
	if (file == NULL || (file->attr & FAT32_ATTR_DIRECTORY))
	{
		return -1;
	}

	if (size == 0)
	{
		return 0;
	}

	fat32_mount_t* m = file->mount;

	if (file->first_cluster < 2)
	{
		uint32_t fresh = fat32_alloc_cluster(m);
		if (fresh == 0)
		{
			return -1;
		}
		file->first_cluster = fresh;
	}

	uint64_t end = offset + size;
	uint32_t needed_clusters = (uint32_t)((end + m->cluster_bytes - 1) / m->cluster_bytes);

	uint32_t cursor = file->first_cluster;
	uint32_t have = 1;

	while (have < needed_clusters)
	{
		uint32_t next = fat32_next_cluster(m, cursor);

		if (next >= FAT32_EOC || next < 2)
		{
			uint32_t fresh = fat32_chain_append(m, cursor);
			if (fresh == 0)
			{
				return -1;
			}
			cursor = fresh;
		}
		else
		{
			cursor = next;
		}

		have++;
	}

	uint32_t cluster = file->first_cluster;
	uint32_t skip = (uint32_t)(offset / m->cluster_bytes);

	for (uint32_t i = 0; i < skip; i++)
	{
		cluster = fat32_next_cluster(m, cluster);
		if (cluster < 2 || cluster >= FAT32_EOC)
		{
			return -1;
		}
	}

	uint64_t in_cluster = offset % m->cluster_bytes;
	const uint8_t* in = (const uint8_t*)buffer;
	uint32_t done = 0;

	while (done < size)
	{
		uint32_t chunk = m->cluster_bytes - (uint32_t)in_cluster;
		if (chunk > size - done)
		{
			chunk = size - done;
		}

		if (fat32_cluster_write(m, cluster, in_cluster, chunk, in + done) != (int)chunk)
		{
			return -1;
		}

		done += chunk;
		in_cluster = 0;

		if (done < size)
		{
			cluster = fat32_next_cluster(m, cluster);
			if (cluster < 2 || cluster >= FAT32_EOC)
			{
				break;
			}
		}
	}

	if (end > file->size)
	{
		file->size = (uint32_t)end;
	}

	node->size = file->size;

	if (fat32_dir_update_entry(m, file->dir_cluster, node->name, file->first_cluster, file->size) != 0)
	{
		return -1;
	}

	return (int)done;
}

static vfs_node_t* fat32_finddir(vfs_node_t* node, const char* name)
{
	fat32_file_t* file = fat32_file_from(node);
	if (file == NULL || !(file->attr & FAT32_ATTR_DIRECTORY))
	{
		return NULL;
	}

	fat32_dirent_t entry;
	if (fat32_dir_lookup(file->mount, file->first_cluster, name, &entry, NULL) != 0)
	{
		return NULL;
	}

	return fat32_node_create(file->mount, name, fat32_cluster_of(&entry), file->first_cluster, entry.size, entry.attr);
}

static int fat32_readdir(vfs_node_t* node, uint32_t index, vfs_node_t* out_node)
{
	fat32_file_t* file = fat32_file_from(node);
	if (file == NULL || !(file->attr & FAT32_ATTR_DIRECTORY))
	{
		return -1;
	}

	fat32_dirent_t entry;
	char name[FAT32_NAME_MAX];

	if (fat32_dir_nth(file->mount, file->first_cluster, index, &entry, name, sizeof(name)) != 0)
	{
		return -1;
	}

	strncpy(out_node->name, name, sizeof(out_node->name) - 1);
	out_node->name[sizeof(out_node->name) - 1] = '\0';
	out_node->type = (entry.attr & FAT32_ATTR_DIRECTORY) ? VFS_DIRECTORY : VFS_FILE;
	out_node->size = entry.size;
	out_node->inode_num = fat32_cluster_of(&entry);
	out_node->operations = (entry.attr & FAT32_ATTR_DIRECTORY) ? &fat32_dir_operations : &fat32_file_operations;
	out_node->private_data = NULL;
	out_node->canonical = NULL;
	out_node->refs = 0;

	return 0;
}

static int fat32_create(vfs_node_t* node, const char* name, uint16_t mask)
{
	(void)mask;

	fat32_file_t* file = fat32_file_from(node);
	if (file == NULL || !(file->attr & FAT32_ATTR_DIRECTORY))
	{
		return -1;
	}

	if (fat32_dir_lookup(file->mount, file->first_cluster, name, NULL, NULL) == 0)
	{
		return -1;
	}

	return fat32_dir_add(file->mount, file->first_cluster, name, FAT32_ATTR_ARCHIVE, 0, 0);
}

static int fat32_mkdir(vfs_node_t* node, const char* name, uint16_t mask)
{
	(void)mask;

	fat32_file_t* file = fat32_file_from(node);
	if (file == NULL || !(file->attr & FAT32_ATTR_DIRECTORY))
	{
		return -1;
	}

	fat32_mount_t* m = file->mount;

	if (fat32_dir_lookup(m, file->first_cluster, name, NULL, NULL) == 0)
	{
		return -1;
	}

	uint32_t fresh = fat32_alloc_cluster(m);
	if (fresh == 0)
	{
		return -1;
	}

	uint8_t blank[FAT32_SECTOR];
	fat32_memzero(blank, sizeof(blank));

	for (uint32_t s = 0; s < m->sectors_per_cluster; s++)
	{
		if (fat32_sector_write(m, fat32_cluster_lba(m, fresh) + s, blank) != 0)
		{
			fat32_set_next(m, fresh, 0);
			return -1;
		}
	}

	fat32_dirent_t dot;
	fat32_dirent_t dotdot;
	fat32_memzero(&dot, sizeof(dot));
	fat32_memzero(&dotdot, sizeof(dotdot));

	for (int i = 0; i < 11; i++)
	{
		dot.name[i] = ' ';
		dotdot.name[i] = ' ';
	}

	dot.name[0] = '.';
	dotdot.name[0] = '.';
	dotdot.name[1] = '.';
	dot.attr = FAT32_ATTR_DIRECTORY;
	dotdot.attr = FAT32_ATTR_DIRECTORY;
	dot.cluster_low = (uint16_t)fresh;
	dotdot.cluster_low = (uint16_t)file->first_cluster;

	if (fat32_cluster_write(m, fresh, 0, sizeof(dot), &dot) != (int)sizeof(dot) ||
		fat32_cluster_write(m, fresh, sizeof(dot), sizeof(dotdot), &dotdot) != (int)sizeof(dotdot))
	{
		fat32_set_next(m, fresh, 0);
		return -1;
	}

	if (fat32_dir_add(m, file->first_cluster, name, FAT32_ATTR_DIRECTORY, fresh, 0) != 0)
	{
		fat32_set_next(m, fresh, 0);
		return -1;
	}

	return 0;
}

static int fat32_remove(vfs_node_t* node, const char* name, int want_dir)
{
	fat32_file_t* file = fat32_file_from(node);
	if (file == NULL || !(file->attr & FAT32_ATTR_DIRECTORY))
	{
		return -1;
	}

	fat32_mount_t* m = file->mount;
	fat32_dirent_t entry;
	fat32_slot_t slot;

	if (fat32_dir_lookup(m, file->first_cluster, name, &entry, &slot) != 0)
	{
		return -1;
	}

	int is_dir = (entry.attr & FAT32_ATTR_DIRECTORY) != 0;

	if (want_dir && !is_dir)
	{
		return -1;
	}

	if (!want_dir && is_dir)
	{
		return -1;
	}

	if (is_dir && fat32_dir_count(m, fat32_cluster_of(&entry)) > 0)
	{
		return -1;
	}

	uint8_t sector[FAT32_SECTOR];
	uint64_t lba = fat32_cluster_lba(m, slot.cluster) + slot.offset / m->bytes_per_sector;

	if (fat32_sector_read(m, lba, sector) != 0)
	{
		return -1;
	}

	sector[slot.offset % m->bytes_per_sector] = FAT32_DIR_FREE;

	if (fat32_sector_write(m, lba, sector) != 0)
	{
		return -1;
	}

	fat32_dir_free_lfn_before(m, slot, fat32_shortname_checksum(entry.name));
	fat32_chain_free(m, fat32_cluster_of(&entry));

	return 0;
}

static int fat32_unlink(vfs_node_t* node, const char* name)
{
	return fat32_remove(node, name, 0);
}

static int fat32_rmdir(vfs_node_t* node, const char* name)
{
	return fat32_remove(node, name, 1);
}

static void fat32_release(vfs_node_t* node)
{
	fat32_file_t* file = (fat32_file_t*)node->private_data;

	if (file != NULL && file->mount != NULL)
	{
		fat32_mount_t* m = file->mount;

		for (uint32_t i = 0; i < m->dir_cache_count; i++)
		{
			if (m->dir_cache[i] == node)
			{
				for (uint32_t j = i + 1; j < m->dir_cache_count; j++)
				{
					m->dir_cache[j - 1] = m->dir_cache[j];
				}
				m->dir_cache_count--;
				break;
			}
		}
	}

	kfree(file);
	kfree(node);
}

static vfs_operations_t fat32_file_operations =
{
	fat32_read,
	fat32_write,
	NULL,
	NULL,
	NULL,
	NULL,
	NULL,
	NULL,
	fat32_release
};

static vfs_operations_t fat32_dir_operations =
{
	NULL,
	NULL,
	fat32_finddir,
	fat32_readdir,
	fat32_create,
	fat32_mkdir,
	fat32_unlink,
	fat32_rmdir,
	fat32_release
};

static int fat32_validate(fat32_bpb_t* bpb, uint8_t* sector)
{
	if (sector[510] != 0x55 || sector[511] != 0xAA)
	{
		return 0;
	}

	if (bpb->bytes_per_sector != FAT32_SECTOR)
	{
		return 0;
	}

	if (bpb->sectors_per_cluster == 0 ||
		(bpb->sectors_per_cluster & (bpb->sectors_per_cluster - 1)) != 0 ||
		bpb->sectors_per_cluster > 128)
	{
		return 0;
	}

	if (bpb->fat_size16 != 0 || bpb->root_entries != 0 || bpb->fat_size32 == 0)
	{
		return 0;
	}

	if (bpb->fat_count == 0 || bpb->reserved_sectors == 0)
	{
		return 0;
	}

	if (bpb->fs_type[0] != 'F' || bpb->fs_type[1] != 'A' ||
		bpb->fs_type[2] != 'T' || bpb->fs_type[3] != '3' ||
		bpb->fs_type[4] != '2')
	{
		return 0;
	}

	return 1;
}

static vfs_node_t* fat32_mount(block_device_t* dev)
{
	if (dev == NULL || dev->read_blocks == NULL || dev->total_sectors < 2)
	{
		return NULL;
	}

	uint8_t sector[FAT32_SECTOR];

	if (dev->read_blocks(dev, 0, 1, sector) != 0)
	{
		return NULL;
	}

	fat32_bpb_t* bpb = (fat32_bpb_t*)sector;

	if (!fat32_validate(bpb, sector))
	{
		return NULL;
	}

	fat32_mount_t* m = kcalloc(1, sizeof(fat32_mount_t));
	if (m == NULL)
	{
		return NULL;
	}

	m->dev = dev;
	m->bytes_per_sector = bpb->bytes_per_sector;
	m->sectors_per_cluster = bpb->sectors_per_cluster;
	m->cluster_bytes = m->bytes_per_sector * m->sectors_per_cluster;
	m->fat_start = bpb->reserved_sectors;
	m->fat_size = bpb->fat_size32;
	m->fat_count = bpb->fat_count;
	m->data_start = m->fat_start + (uint64_t)bpb->fat_count * m->fat_size;
	m->root_cluster = bpb->root_cluster;
	m->total_sectors = (bpb->total_sectors32 != 0) ? bpb->total_sectors32 : bpb->total_sectors16;
	m->alloc_hint = 2;

	if (m->total_sectors == 0)
	{
		kfree(m);
		return NULL;
	}

	if (m->total_sectors > dev->total_sectors)
	{
		serial_write("FAT32: volume claims ");
		serial_dec(m->total_sectors);
		serial_write(" sectors, device has ");
		serial_dec(dev->total_sectors);
		serial_write("; clamping\r\n");

		m->total_sectors = dev->total_sectors;
	}

	if (m->root_cluster < 2 || m->data_start >= m->total_sectors)
	{
		kfree(m);
		return NULL;
	}

	m->cluster_count = (uint32_t)((m->total_sectors - m->data_start) / m->sectors_per_cluster);

	if (m->cluster_count == 0)
	{
		kfree(m);
		return NULL;
	}

	for (int i = 0; i < 11; i++)
	{
		m->label[i] = bpb->volume_label[i];
	}
	m->label[11] = '\0';

	if (fat32_load_fat(m) != 0)
	{
		kfree(m->fat);
		kfree(m);
		return NULL;
	}

	vfs_node_t* root = fat32_node_create(m, "/", m->root_cluster, m->root_cluster, 0, FAT32_ATTR_DIRECTORY);
	if (root == NULL)
	{
		kfree(m->fat);
		kfree(m);
		return NULL;
	}

	root->refs = 1;
	m->root = root;

	if (fat32_volume_count < FAT32_MAX_VOLUMES)
	{
		fat32_volumes[fat32_volume_count++] = m;
	}

	serial_write("FAT32: mounted ");
	serial_write(dev->name);
	serial_write(" label=");
	serial_write(m->label);
	serial_write(" clusters=");
	serial_dec(m->cluster_count);
	serial_write(" fat=");
	serial_dec(m->fat_entries);
	serial_write("\r\n");

	return root;
}

void fat32_init(void)
{
	fat32_driver.name = "fat32";
	fat32_driver.mount = fat32_mount;

	fs_register(&fat32_driver);
}
