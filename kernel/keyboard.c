#include "keyboard.h"
#include "heap.h"
#include "serial.h"
#include "scheduler.h"

#define KEYBOARD_BUFFER_SIZE 256

static uint8_t *buffer = 0;
static uint64_t head = 0;
static uint64_t tail = 0;
static uint64_t dropped = 0;

thread_t* keyboard_waiting_thread = NULL;

static uint64_t interrupts_off(void)
{
	uint64_t flags;
	__asm__ volatile("pushfq; pop %0" : "=r"(flags));
	__asm__ volatile("cli");
	return flags;
}

static void interrupts_restore(uint64_t flags)
{
	if (flags & (1ULL << 9))
	{
		__asm__ volatile("sti");
	}
}

void keyboard_init(void)
{
	buffer = (uint8_t *)kcalloc(KEYBOARD_BUFFER_SIZE, sizeof(uint8_t));
	if (buffer == 0)
	{
		serial_write("keyboard: cannot allocate buffer\r\n");
		return;
	}

	head = 0;
	tail = 0;
	dropped = 0;

	serial_field("Keyboard buffer at ", (uint64_t)buffer);
}

void keyboard_push(uint8_t scancode)
{
	if (buffer == 0)
	{
		return;
	}

	uint64_t flags = interrupts_off();

	uint64_t next = (head + 1) % KEYBOARD_BUFFER_SIZE;
	if (next == tail)
	{
		dropped++;
	}
	else
	{
		buffer[head] = scancode;
		head = next;
	}

	interrupts_restore(flags);
}

int keyboard_pop(uint8_t *scancode)
{
	if (buffer == 0 || scancode == 0)
	{
		return 0;
	}

	uint64_t flags = interrupts_off();

	int found = 0;
	if (tail != head)
	{
		*scancode = buffer[tail];
		tail = (tail + 1) % KEYBOARD_BUFFER_SIZE;
		found = 1;
	}

	interrupts_restore(flags);

	return found;
}

uint64_t keyboard_count(void)
{
	if (buffer == 0)
	{
		return 0;
	}

	uint64_t flags = interrupts_off();
	uint64_t count = (head + KEYBOARD_BUFFER_SIZE - tail) % KEYBOARD_BUFFER_SIZE;
	interrupts_restore(flags);

	return count;
}
