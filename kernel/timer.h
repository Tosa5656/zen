#pragma once

#ifndef TIMER_H
#define TIMER_H

#include <stdint.h>

#define PIT_COMMAND_PORT 0x43
#define PIT_DATA_PORT_CH0 0x40

extern volatile uint64_t system_ticks;

void timer_init(uint32_t frequency);
void timer_handler(void);
uint64_t timer_get_ticks(void);
void sleep(uint64_t milliseconds);

#endif
