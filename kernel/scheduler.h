#ifndef SCHEDULER_H
#define SCHEDULER_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <idt.h>

typedef enum
{
	PROCESS_READY,
	PROCESS_RUNNING,
	PROCESS_BLOCKED,
	PROCESS_DEAD
} process_state_t;

typedef struct process
{
	cpu_registers_t* context;
	void* kernel_stack_base;
	uint64_t page_directory;
	struct fd_table* fds;
	uint64_t id;
	process_state_t state;
	uint64_t sleep_until_ticks;
	uint64_t blocked_rip;
	uint64_t blocked_rax;
	struct process* next;
	struct process* wait_next;
	uint64_t brk_start;
	uint64_t program_break;
} process_t;

void scheduler_init(void);
void scheduler_enqueue(process_t* process);
process_t* scheduler_dequeue(void);
void scheduler_wake(process_t* process);
process_t* process_create_user(void* entry_point, uint64_t pml4_physical, uint64_t user_rsp);
uint64_t schedule_callback(uint64_t old_rsp);
bool scheduler_should_switch(void);
void scheduler_sleep_enqueue(process_t* process);
void scheduler_check_sleeping(void);
void process_kill_current(void);
void process_reclaim_dead(void);

extern process_t* current_process;

#endif
