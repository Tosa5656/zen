#include "framebuffer.h"
#include "memory.h"
#include "serial.h"
#include "vfs.h"
#include "heap.h"
#include "psf.h"

static uint8_t* framebuffer = NULL;
static uint32_t framebuffer_width = 0;
static uint32_t framebuffer_height = 0;
static uint32_t framebuffer_pitch = 0;
static uint8_t framebuffer_bpp = 0;
static uint8_t framebuffer_red_shift = 0;
static uint8_t framebuffer_green_shift = 0;
static uint8_t framebuffer_blue_shift = 0;

static uint8_t* font_glyphs = NULL;
static uint32_t font_width = 0;
static uint32_t font_height = 0;
static uint32_t font_bytes_per_glyph = 0;
static uint32_t font_num_glyphs = 0;
static uint8_t* font_raw_buffer = NULL;
static uint32_t font_raw_size = 0;

static glyph_entry_t* glyph_map = NULL;
static uint32_t glyph_map_count = 0;
static uint32_t glyph_map_capacity = 0;
static int font_has_unicode_map = 0;
static int framebuffer_ready = 0;

static uint32_t cursor_x = 0;
static uint32_t cursor_y = 0;

static uint8_t* framebuffer_read_font(const char* font_path, uint32_t* out_size)
{
	if (font_path == NULL || out_size == NULL)
	{
		return NULL;
	}

	vfs_node_t* node = vfs_open(font_path, VFS_RDONLY);
	if (node == NULL || node->type != VFS_FILE)
	{
		vfs_close(node);
		return NULL;
	}

	uint32_t size = (uint32_t)node->size;

	if (size == 0)
	{
		vfs_close(node);
		return NULL;
	}

	uint8_t* data = kmalloc(size);

	if (data == NULL)
	{
		vfs_close(node);
		return NULL;
	}

	int moved = vfs_read(node, 0, size, data);
	vfs_close(node);

	if (moved != (int)size)
	{
		kfree(data);
		return NULL;
	}

	*out_size = size;
	return data;
}

static int font_utf8_next(const uint8_t** cursor, const uint8_t* end, uint32_t* out_codepoint)
{
	const uint8_t* p = *cursor;

	if (p >= end)
	{
		return 0;
	}

	uint8_t lead = *p;
	uint32_t codepoint;
	uint32_t extra;

	if ((lead & 0x80) == 0)
	{
		codepoint = lead;
		extra = 0;
	}
	else if ((lead & 0xE0) == 0xC0)
	{
		codepoint = lead & 0x1F;
		extra = 1;
	}
	else if ((lead & 0xF0) == 0xE0)
	{
		codepoint = lead & 0x0F;
		extra = 2;
	}
	else if ((lead & 0xF8) == 0xF0)
	{
		codepoint = lead & 0x07;
		extra = 3;
	}
	else
	{
		*cursor = p + 1;
		return 0;
	}

	p++;

	for (uint32_t i = 0; i < extra; i++)
	{
		if (p >= end || (*p & 0xC0) != 0x80)
		{
			*cursor = p;
			return 0;
		}

		codepoint = (codepoint << 6) | (*p & 0x3F);
		p++;
	}

	*cursor = p;
	*out_codepoint = codepoint;

	return 1;
}

static int glyph_map_reserve(uint32_t needed)
{
	if (needed <= glyph_map_capacity)
	{
		return 1;
	}

	uint32_t capacity = glyph_map_capacity == 0 ? 16 : glyph_map_capacity;

	while (capacity < needed)
	{
		capacity *= 2;
	}

	glyph_entry_t* grown = krealloc(glyph_map, (uint64_t)capacity * sizeof(glyph_entry_t));

	if (grown == NULL)
	{
		return 0;
	}

	glyph_map = grown;
	glyph_map_capacity = capacity;

	return 1;
}

static int glyph_map_push(uint32_t codepoint, uint32_t glyph)
{
	if (!glyph_map_reserve(glyph_map_count + 1))
	{
		return 0;
	}

	glyph_map[glyph_map_count].codepoint = codepoint;
	glyph_map[glyph_map_count].glyph = (uint16_t)glyph;
	glyph_map_count++;

	return 1;
}

static int glyph_map_lookup(uint32_t codepoint, uint32_t* out_glyph)
{
	uint32_t low = 0;
	uint32_t high = glyph_map_count;

	while (low < high)
	{
		uint32_t mid = low + (high - low) / 2;

		if (glyph_map[mid].codepoint == codepoint)
		{
			*out_glyph = glyph_map[mid].glyph;
			return 1;
		}

		if (glyph_map[mid].codepoint < codepoint)
		{
			low = mid + 1;
		}
		else
		{
			high = mid;
		}
	}

	return 0;
}

static void glyph_map_sort(void)
{
	for (uint32_t i = 1; i < glyph_map_count; i++)
	{
		glyph_entry_t entry = glyph_map[i];
		uint32_t j = i;

		while (j > 0 && glyph_map[j - 1].codepoint > entry.codepoint)
		{
			glyph_map[j] = glyph_map[j - 1];
			j--;
		}

		glyph_map[j] = entry;
	}
}

static int font_build_unicode_map(void)
{
	glyph_map_count = 0;

	if (font_glyphs == NULL || font_raw_buffer == NULL || font_num_glyphs == 0)
	{
		return 0;
	}

	if (!glyph_map_reserve(font_num_glyphs))
	{
		serial_write("Out of memory for framebuffer glyph map!\r\n");
		return 0;
	}

	const uint8_t* end = font_raw_buffer + font_raw_size;
	const uint8_t* p = font_glyphs + (uint64_t)font_num_glyphs * font_bytes_per_glyph;
	uint32_t glyph = 0;

	while (p < end && glyph < font_num_glyphs)
	{
		if (*p == PSF2_SEPARATOR)
		{
			glyph++;
			p++;
			continue;
		}

		int in_sequence = 0;

		while (p < end && *p != PSF2_SEPARATOR)
		{
			if (*p == PSF2_START_SEQ)
			{
				in_sequence = 1;
				p++;
				continue;
			}

			uint32_t codepoint = 0;
			if (!font_utf8_next(&p, end, &codepoint))
			{
				continue;
			}

			if (!in_sequence)
			{
				glyph_map_push(codepoint, glyph);
			}
		}
	}

	glyph_map_sort();

	uint32_t unique = 0;

	for (uint32_t i = 0; i < glyph_map_count; i++)
	{
		if (unique > 0 && glyph_map[unique - 1].codepoint == glyph_map[i].codepoint)
		{
			continue;
		}

		glyph_map[unique++] = glyph_map[i];
	}

	glyph_map_count = unique;

	return glyph_map_count > 0;
}

uint32_t framebuffer_make_color(uint8_t red, uint8_t green, uint8_t blue)
{
	return ((uint32_t)red << framebuffer_red_shift) |
		   ((uint32_t)green << framebuffer_green_shift) |
		   ((uint32_t)blue << framebuffer_blue_shift);
}

void framebuffer_put_pixel(uint32_t x, uint32_t y, uint32_t color)
{
	if (framebuffer == NULL || x >= framebuffer_width || y >= framebuffer_height)
	{
		return;
	}

	uint32_t bytes_per_pixel = framebuffer_bpp / 8;
	uint8_t* pixel = framebuffer + (uint64_t)y * framebuffer_pitch + (uint64_t)x * bytes_per_pixel;

	switch (framebuffer_bpp)
	{
		case 32:
			*(uint32_t*)pixel = color;
			break;

		case 24:
			pixel[0] = (uint8_t)(color & 0xFF);
			pixel[1] = (uint8_t)((color >> 8) & 0xFF);
			pixel[2] = (uint8_t)((color >> 16) & 0xFF);
			break;

		case 16:
			*(uint16_t*)pixel = (uint16_t)color;
			break;

		default:
			break;
	}
}

void framebuffer_put_char(uint32_t codepoint, uint32_t fg_color, uint32_t bg_color)
{
	if (codepoint == '\n')
	{
		cursor_x = 0;
		cursor_y += font_height;
		return;
	}
	if (codepoint == '\r')
	{
		cursor_x = 0;
		return;
	}
	if (codepoint == '\t')
	{
		cursor_x += font_width * 4;
		if (cursor_x + font_width > framebuffer_width)
		{
			cursor_x = 0;
			cursor_y += font_height;
		}
		return;
	}

	if (cursor_y + font_height > framebuffer_height)
	{
		framebuffer_clear_screen(bg_color);
		cursor_x = 0;
		cursor_y = 0;
	}

	uint32_t glyph_idx = 0;
	int found = 0;

	if (font_has_unicode_map)
	{
		found = glyph_map_lookup(codepoint, &glyph_idx);
	}
	else if (codepoint < font_num_glyphs)
	{
		glyph_idx = codepoint;
		found = 1;
	}

	if (!found && font_has_unicode_map)
	{
		found = glyph_map_lookup(' ', &glyph_idx);
	}

	if (font_glyphs != NULL && found && glyph_idx < font_num_glyphs)
	{
		uint8_t* glyph = font_glyphs + (glyph_idx * font_bytes_per_glyph);
		uint32_t bytes_per_line = (font_width + 7) / 8;

		for (uint32_t y = 0; y < font_height; y++)
		{
			for (uint32_t x = 0; x < font_width; x++)
			{
				uint8_t byte = glyph[y * bytes_per_line + (x / 8)];
				uint32_t current_color = (byte & (0x80 >> (x & 7))) ? fg_color : bg_color;
				framebuffer_put_pixel(cursor_x + x, cursor_y + y, current_color);
			}
		}
	}

	cursor_x += font_width;

	if (cursor_x + font_width > framebuffer_width)
	{
		cursor_x = 0;
		cursor_y += font_height;
	}
}

void framebuffer_print_string(const char* str, uint32_t fg_color, uint32_t bg_color)
{
	if (str == NULL) return;

	const uint8_t* s = (const uint8_t*)str;
	const uint8_t* end = s;

	while (*end != '\0')
	{
		end++;
	}

	while (s < end)
	{
		uint32_t codepoint = 0;

		if (!font_utf8_next(&s, end, &codepoint))
		{
			continue;
		}

		framebuffer_put_char(codepoint, fg_color, bg_color);
	}
}

void framebuffer_console_write(const char* data, uint32_t size)
{
	if (data == NULL || size == 0 || !framebuffer_ready)
	{
		return;
	}

	uint32_t fg = framebuffer_make_color(255, 255, 255);
	uint32_t bg = framebuffer_make_color(0, 0, 0);

	const uint8_t* p = (const uint8_t*)data;
	const uint8_t* end = p + size;

	while (p < end)
	{
		uint32_t codepoint = 0;

		if (!font_utf8_next(&p, end, &codepoint))
		{
			continue;
		}

		framebuffer_put_char(codepoint, fg, bg);
	}
}

void framebuffer_clear_screen(uint32_t color)
{
	for (uint32_t y = 0; y < framebuffer_height; y++)
	{
		for (uint32_t x = 0; x < framebuffer_width; x++)
		{
			framebuffer_put_pixel(x, y, color);
		}
	}
}

void framebuffer_init(const boot_info_t *info, const char* font_path)
{
	if (info == NULL || info->framebuffer_address == 0)
	{
		serial_write("Framebuffer boot info has no framebuffer!\r\n");
		return;
	}

	if (info->framebuffer_bpp != 16 && info->framebuffer_bpp != 24 &&
		info->framebuffer_bpp != 32)
	{
		serial_write("Unsupported framebuffer BPP!\r\n");
		return;
	}

	if (info->framebuffer_pitch < (uint64_t)info->framebuffer_width * (info->framebuffer_bpp / 8))
	{
		serial_write("Framebuffer pitch too small for width!\r\n");
		return;
	}

	framebuffer = mmio_to_virtual(info->framebuffer_address);
	framebuffer_width = info->framebuffer_width;
	framebuffer_height = info->framebuffer_height;
	framebuffer_pitch = info->framebuffer_pitch;
	framebuffer_bpp = info->framebuffer_bpp;
	framebuffer_red_shift = info->framebuffer_red_mask_shift;
	framebuffer_green_shift = info->framebuffer_green_mask_shift;
	framebuffer_blue_shift = info->framebuffer_blue_mask_shift;

	serial_write("Framebuffer mapped at virtual base, clearing.\r\n");

	framebuffer_clear_screen(framebuffer_make_color(0, 0, 0));

	uint32_t font_size = 0;
	uint8_t* font = framebuffer_read_font(font_path, &font_size);

	if (font == NULL)
	{
		serial_write("Font for framebuffer not loaded\r\n");
		return;
	}

	if (font_size < sizeof(psf2_header_t))
	{
		serial_write("Font file is too small to be PSF2\r\n");
		kfree(font);
		return;
	}

	psf2_header_t* header = (psf2_header_t*)font;

	if (header->magic != PSF2_MAGIC)
	{
		serial_write("Invalid PSF2 magic number for framebuffer font!\r\n");
		kfree(font);
		return;
	}

	uint64_t glyph_bytes = (uint64_t)header->numglyph * header->bytesperglyph;

	if (header->headersize < sizeof(psf2_header_t) ||
		header->numglyph == 0 || header->numglyph > 0xFFFF ||
		header->width == 0 || header->height == 0 ||
		header->bytesperglyph < (uint64_t)((header->width + 7) / 8) * header->height ||
		(uint64_t)header->headersize + glyph_bytes > font_size)
	{
		serial_write("Framebuffer font header is inconsistent!\r\n");
		kfree(font);
		return;
	}

	font_raw_buffer = font;
	font_raw_size = font_size;
	font_width = header->width;
	font_height = header->height;
	font_bytes_per_glyph = header->bytesperglyph;
	font_num_glyphs = header->numglyph;
	font_glyphs = font + header->headersize;

	if (header->flags & 1)
	{
		font_has_unicode_map = font_build_unicode_map();

		serial_write("Framebuffer glyph map: ");
		serial_dec(glyph_map_count);
		serial_write(" codepoints, ");
		serial_dec(glyph_map_capacity * sizeof(glyph_entry_t));
		serial_write(" bytes\r\n");
	}
	else
	{
		font_has_unicode_map = 0;
	}

	serial_write("Framebuffer font initialized successfully.\r\n");

	framebuffer_ready = 1;

	framebuffer_print_string("Framebuffer successfully inited!\r\n", framebuffer_make_color(255, 255, 255), framebuffer_make_color(0, 0, 0));
}
