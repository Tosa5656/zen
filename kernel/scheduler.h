#ifndef SCHEDULER_H
#define SCHEDULER_H

#include <stdint.h>
#include <stddef.h>
#include "idt.h"


typedef enum
{
	THREAD_READY,
	THREAD_RUNNING,
	THREAD_BLOCKED
} thread_state_t;

typedef struct thread
{
	cpu_registers_t* context;
	void* kernel_stack_base;
	uint64_t page_directory;
	uint64_t id;
	thread_state_t state;
	struct thread* next;
} thread_t;

void scheduler_init(void);
thread_t* scheduler_create_user_thread(void* entry_point, uint64_t pml4_physical, uint64_t user_rsp);
uint64_t schedule_callback(uint64_t old_rsp);

#endif
