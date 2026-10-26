#include "scheduler.h"
#include "memory.h"
#include "gdt.h"
#include "serial.h"
#include "idt.h"
#include "msr.h"
#include "timer.h"
#include "spinlock.h"
#include "fd.h"
#include <string.h>

process_t* current_process = NULL;
process_t* idle_process = NULL;
process_t* ready_queue_head = NULL;
process_t* ready_queue_tail = NULL;
process_t* sleep_queue_head = NULL;
process_t* dead_queue_head = NULL;
static process_t* zombie_queue_head = NULL;
static process_t* zombie_waiters_head = NULL;

extern volatile uint64_t system_ticks;

static uint64_t next_pid = 1;
static spinlock_t scheduler_lock = {0};

static void scheduler_idle_loop(void)
{
	__asm__ volatile("sti");

	while(1)
	{
		process_reclaim_dead();

		__asm__ volatile("hlt");
	}
}

void scheduler_enqueue(process_t* process)
{
	if (process == NULL)
	{
		return;
	}

	spin_lock(&scheduler_lock);

	if (process == ready_queue_tail)
	{
		spin_unlock(&scheduler_lock);
		return;
	}

	process->state = PROCESS_READY;
	process->next = NULL;
	if(!ready_queue_head)
	{
		ready_queue_head = process;
		ready_queue_tail = process;
	}
	else
	{
		ready_queue_tail->next = process;
		ready_queue_tail = process;
	}

	spin_unlock(&scheduler_lock);
}

process_t* scheduler_dequeue(void)
{
	spin_lock(&scheduler_lock);

	process_t* process = NULL;
	if (ready_queue_head)
	{
		process = ready_queue_head;
		ready_queue_head = ready_queue_head->next;
		if (!ready_queue_head)
		{
			ready_queue_tail = NULL;
		}
	}

	spin_unlock(&scheduler_lock);
	return process;
}

void scheduler_wake(process_t* process)
{
	if (process == NULL)
	{
		return;
	}

	spin_lock(&scheduler_lock);

	if (process->state == PROCESS_BLOCKED && process->context != NULL &&
		process->blocked_rip >= 2)
	{
		process->context->rip = process->blocked_rip - 2;
		process->context->rax = process->blocked_rax;
		process->blocked_rip = 0;
	}

	spin_unlock(&scheduler_lock);

	scheduler_enqueue(process);
}

void scheduler_sleep_enqueue(process_t* process)
{
	process->state = PROCESS_BLOCKED;
	process->next = sleep_queue_head;
	sleep_queue_head = process;
}

void scheduler_check_sleeping(void)
{
	process_t* previous = NULL;
	process_t* current = sleep_queue_head;

	while (current != NULL)
	{
		if (system_ticks >= current->sleep_until_ticks)
		{
			process_t* next = current->next;
			if (previous == NULL)
			{
				sleep_queue_head = next;
			}
			else
			{
				previous->next = next;
			}

			scheduler_enqueue(current);
			serial_write("nanosleep: woken process at tick ");
			serial_dec(current->sleep_until_ticks);
			serial_write("\r\n");

			current = next;
		}
		else
		{
			previous = current;
			current = current->next;
		}
	}
}


process_t* process_create_user(void* entry_point, uint64_t pml4_physical, uint64_t user_rsp)
{
	uint64_t tcb_physical = physical_memory_manager_alloc_page();
	process_t* process = (process_t*)physical_to_virtual(tcb_physical);

	memset(process, 0, sizeof(*process));

	uint64_t kstack_physical = physical_memory_manager_alloc_page();
	void* kstack_virtual = physical_to_virtual(kstack_physical);

	process->kernel_stack_base = kstack_virtual;
	process->page_directory = pml4_physical;
	process->id = next_pid++;

	cpu_registers_t* ctx = (cpu_registers_t*)((uintptr_t)kstack_virtual + 4096 - sizeof(cpu_registers_t));

	ctx->ss = GDT_UDATA;
	ctx->rsp = user_rsp;
	ctx->rflags = 0x202;
	ctx->cs = GDT_UCODE;
	ctx->rip = (uint64_t)entry_point;
	ctx->interrupt_number = 32;
	ctx->error_code = 0;

	ctx->rax = ctx->rbx = ctx->rcx = ctx->rdx = 0;
	ctx->rsi = ctx->rdi = ctx->rbp = 0;
	ctx->r8 = ctx->r9 = ctx->r10 = ctx->r11 = 0;
	ctx->r12 = ctx->r13 = ctx->r14 = ctx->r15 = 0;

	process->context = ctx;
	process->fds = fd_table_create_default();

	scheduler_enqueue(process);
	return process;
}

void process_kill_current(void)
{
	spin_lock(&scheduler_lock);

	current_process->state = PROCESS_ZOMBIE;

	current_process->zombie_next = zombie_queue_head;
	zombie_queue_head = current_process;

	process_t* waiter = zombie_waiters_head;
	while (waiter != NULL)
	{
		process_t* next = waiter->zombie_wait_next;
		waiter->zombie_wait_next = NULL;
		scheduler_wake(waiter);
		waiter = next;
	}
	zombie_waiters_head = NULL;

	spin_unlock(&scheduler_lock);
}

static process_t** zombie_find_slot(int64_t pid)
{
	process_t** slot = &zombie_queue_head;

	while (*slot != NULL)
	{
		if (pid <= 0 || (*slot)->id == (uint64_t)pid)
		{
			return slot;
		}
		slot = &(*slot)->zombie_next;
	}

	return NULL;
}

process_t* process_take_zombie(int64_t pid)
{
	spin_lock(&scheduler_lock);

	process_t** slot = zombie_find_slot(pid);
	if (slot == NULL)
	{
		spin_unlock(&scheduler_lock);
		return NULL;
	}

	process_t* zombie = *slot;
	*slot = zombie->zombie_next;
	zombie->zombie_next = NULL;

	spin_unlock(&scheduler_lock);
	return zombie;
}

void process_release(process_t* process)
{
	if (process == NULL)
	{
		return;
	}

	if (process->fds != NULL)
	{
		fd_table_destroy(process->fds);
		process->fds = NULL;
	}

	uint64_t kstack_physical = VIRTUAL_TO_PHYSICAL(process->kernel_stack_base);
	physical_memory_manager_free_page(kstack_physical);

	uint64_t tcb_physical = VIRTUAL_TO_PHYSICAL(process);
	physical_memory_manager_free_page(tcb_physical);
}

void process_wait_enqueue(process_t* process)
{
	spin_lock(&scheduler_lock);

	process->zombie_wait_next = zombie_waiters_head;
	zombie_waiters_head = process;

	spin_unlock(&scheduler_lock);
}

void process_reclaim_dead(void)
{
	if (!dead_queue_head) return;

	spin_lock(&scheduler_lock);
	process_t* process = dead_queue_head;
	dead_queue_head = dead_queue_head->next;
	spin_unlock(&scheduler_lock);

	if (process->fds != NULL)
	{
		fd_table_destroy(process->fds);
		process->fds = NULL;
	}

	uint64_t kstack_physical = VIRTUAL_TO_PHYSICAL(process->kernel_stack_base);
	physical_memory_manager_free_page(kstack_physical);

	uint64_t tcb_physical = VIRTUAL_TO_PHYSICAL(process);
	physical_memory_manager_free_page(tcb_physical);

	serial_write("Kernel resources of dead process reclaimed.\r\n");
}

uint64_t schedule_callback(uint64_t old_rsp)
{
	if (!current_process) return old_rsp;
	current_process->context = (cpu_registers_t*)old_rsp;

	if (current_process->state == PROCESS_RUNNING && current_process != idle_process
	    && current_process->kernel_stack_base != NULL)
	{
		scheduler_enqueue(current_process);
	}

	process_t* next_proc = scheduler_dequeue();
	if (!next_proc)
	{
		if(current_process->state == PROCESS_RUNNING)
		{
			next_proc = current_process;
		}
		else
		{
			next_proc = idle_process;
		}
	}

	process_t* previous = current_process;
	next_proc->state = PROCESS_RUNNING;
	current_process = next_proc;

	uint64_t active_cr3 = virtual_memory_manager_get_current_pml4();
	if (active_cr3 != next_proc->page_directory)
	{
		virtual_memory_manager_switch_address_space(next_proc->page_directory);
	}

	if (next_proc != previous)
	{
		tss_set_rsp0((uint64_t)next_proc->kernel_stack_base + 4096);

		uint64_t gs_base_address = read_msr(IA32_GS_BASE_MSR);
		if (gs_base_address != 0)
		{
			uint64_t* kernel_rsp_field = (uint64_t*)gs_base_address;
			*kernel_rsp_field = (uint64_t)next_proc->kernel_stack_base + 4096;
		}
	}

	return (uint64_t)next_proc->context;
}

bool scheduler_should_switch(void)
{
	process_t* t = ready_queue_head;
	while (t != NULL)
	{
		if (t != current_process)
		{
			return true;
		}
		t = t->next;
	}

	return false;
}

void scheduler_init(void)
{
	uint64_t tcb_physical = physical_memory_manager_alloc_page();
	process_t* boot_proc = (process_t*)physical_to_virtual(tcb_physical);

	boot_proc->id = 0;
	boot_proc->state = PROCESS_RUNNING;
	boot_proc->next = NULL;
	boot_proc->kernel_stack_base = NULL;
	boot_proc->page_directory = virtual_memory_manager_get_kernel_pml4();

	current_process = boot_proc;

	static uint8_t idle_kstack[16384] __attribute__((aligned(16)));
	void* kstack_idle_virtual = idle_kstack;

	uint64_t tcb_idle_physical = physical_memory_manager_alloc_page();
	idle_process = (process_t*)physical_to_virtual(tcb_idle_physical);

	idle_process->id = 0xFFFFFFFF;
	idle_process->state = PROCESS_READY;
	idle_process->next = NULL;
	idle_process->kernel_stack_base = kstack_idle_virtual;
	idle_process->page_directory = virtual_memory_manager_get_kernel_pml4();

	cpu_registers_t* ctx = (cpu_registers_t*)((uintptr_t)kstack_idle_virtual + 16384 - sizeof(cpu_registers_t));

	ctx->ss = GDT_KDATA;
	ctx->rsp = (uint64_t)kstack_idle_virtual + 16384 - 8;
	ctx->rflags = 0x202;
	ctx->cs = GDT_KCODE;
	ctx->rip = (uint64_t)scheduler_idle_loop;
	ctx->interrupt_number = 32;
	ctx->error_code = 0;

	ctx->rax = ctx->rbx = ctx->rcx = ctx->rdx = 0;
	ctx->rsi = ctx->rdi = ctx->rbp = 0;
	ctx->r8 = ctx->r9 = ctx->r10 = ctx->r11 = 0;
	ctx->r12 = ctx->r13 = ctx->r14 = ctx->r15 = 0;

	idle_process->context = ctx;

	boot_proc->fds = fd_table_create_default();

	serial_write("Scheduler initialized!\r\n");
}
