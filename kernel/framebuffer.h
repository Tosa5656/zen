#ifndef FRAMEBUFFER_H
#define FRAMEBUFFER_H

#include <stdint.h>
#include "boot_info.h"

void framebuffer_init(const boot_info_t *info, const char* font_path);
uint32_t framebuffer_make_color(uint8_t red, uint8_t green, uint8_t blue);
void framebuffer_put_pixel(uint32_t x, uint32_t y, uint32_t color);
void framebuffer_put_char(uint32_t codepoint, uint32_t fg_color, uint32_t bg_color);
void framebuffer_print_string(const char* str, uint32_t fg_color, uint32_t bg_color);
void framebuffer_clear_screen(uint32_t color);

#endif
