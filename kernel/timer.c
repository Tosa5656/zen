#include "timer.h"
#include "io.h"

static volatile uint64_t system_ticks = 0;

void timer_init(uint32_t frequency)
{
	uint32_t divisor = 1193182 / frequency;

	outb(PIT_COMMAND_PORT, 0x36);

	outb(PIT_DATA_PORT_CH0, (uint8_t)(divisor & 0xFF));
	outb(PIT_DATA_PORT_CH0, (uint8_t)((divisor >> 8) & 0xFF));
}

void timer_handler(void)
{
	system_ticks++;

	if (system_ticks % 100 == 0) {}
}

uint64_t timer_get_ticks(void)
{
	return system_ticks;
}

void sleep(uint64_t milliseconds)
{
	uint64_t ticks_to_wait = milliseconds / 10;
	if (ticks_to_wait == 0)
	{
		ticks_to_wait = 1;
	}

	uint64_t target_ticks = system_ticks + ticks_to_wait;

	while(system_ticks < target_ticks)
	{
		__asm__ volatile("hlt");
	}
}
