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

void framebuffer_put_char(char c, uint32_t fg_color, uint32_t bg_color)
{
	if (c == '\n')
	{
		cursor_x = 0;
		cursor_y += font_height;
		return;
	}
	if (c == '\r')
	{
		cursor_x = 0;
		return;
	}
	if (c == '\t')
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

	if (font_glyphs != NULL && (unsigned char)c < font_num_glyphs)
	{
		uint8_t* glyph = font_glyphs + ((unsigned char)c * font_bytes_per_glyph);
		uint32_t bytes_per_line = (font_width + 7) / 8;

		for (uint32_t y = 0; y < font_height; y++)
		{
			for (uint32_t x = 0; x < font_width; x++)
			{
				uint8_t byte = glyph[y * bytes_per_line + (x / 8)];
				uint8_t bit_mask = 0x80 >> (x % 8);
				uint32_t current_color = (byte & bit_mask) ? fg_color : bg_color;
				
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

    while (*str)
	{
		framebuffer_put_char(*str, fg_color, bg_color);
		str++;
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

	font_raw_buffer = font;
	font_width = header->width;
	font_height = header->height;
	font_bytes_per_glyph = header->bytesperglyph;
	font_num_glyphs = header->numglyph;
	font_glyphs = font + header->headersize;

	serial_write("Framebuffer font ");
	serial_dec(font_size);
	serial_write(" bytes, first byte ");
	serial_hex(font[0]);
	serial_write("\r\n");

	framebuffer_print_string("Framebuffer successfully inited!", framebuffer_make_color(255, 255, 255), framebuffer_make_color(0, 0, 0));
}