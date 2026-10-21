#ifndef SCHEDULER_H
#define SCHEDULER_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <idt.h>

typedef enum
{
	THREAD_READY,
	THREAD_RUNNING,
	THREAD_BLOCKED,
	THREAD_DEAD
} thread_state_t;

typedef struct thread
{
	cpu_registers_t* context;
	void* kernel_stack_base;
	uint64_t page_directory;
	struct fd_table* fds;
	uint64_t id;
	thread_state_t state;
	uint64_t sleep_until_ticks;
	struct thread* next;
} thread_t;

void scheduler_init(void);
void scheduler_enqueue(thread_t* thread);
thread_t* scheduler_dequeue(void);
thread_t* scheduler_create_user_thread(void* entry_point, uint64_t pml4_physical, uint64_t user_rsp);
uint64_t schedule_callback(uint64_t old_rsp);
bool scheduler_should_switch(void);
void scheduler_sleep_enqueue(thread_t* thread);
void scheduler_check_sleeping_threads(void);
void scheduler_kill_current_thread(void);
void scheduler_reclaim_dead_threads(void);

extern thread_t* current_thread;

#endif
