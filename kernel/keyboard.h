#ifndef KEYBOARD_H
#define KEYBOARD_H

#include <stdint.h>

void keyboard_init(void);

void keyboard_push(uint8_t scancode);
int keyboard_pop(uint8_t *scancode);
uint64_t keyboard_count(void);

#endif
