#include "scheduler.h"
#include "memory.h"
#include "gdt.h"
#include "serial.h"
#include "idt.h"
#include "msr.h"
#include "timer.h"
#include "spinlock.h"
#include "fd.h"

thread_t* current_thread = NULL;
thread_t* idle_thread = NULL;
thread_t* ready_queue_head = NULL;
thread_t* ready_queue_tail = NULL;
thread_t* sleep_queue_head = NULL;
thread_t* dead_queue_head = NULL;

extern volatile uint64_t system_ticks;

static uint64_t next_thread_id = 1;
static spinlock_t scheduler_lock = {0};

static void scheduler_idle_loop(void)
{
	__asm__ volatile("sti");

	while(1)
	{
		scheduler_reclaim_dead_threads();

		__asm__ volatile("hlt");
	}
}

void scheduler_enqueue(thread_t* thread)
{
	spin_lock(&scheduler_lock);

	thread->state = THREAD_READY;
	thread->next = NULL;
	if(!ready_queue_head)
	{
		ready_queue_head = thread;
		ready_queue_tail = thread;
	}
	else
	{
		ready_queue_tail->next = thread;
		ready_queue_tail = thread;
	}

	spin_unlock(&scheduler_lock);
}

thread_t* scheduler_dequeue(void)
{
	spin_lock(&scheduler_lock);

	thread_t* thread = NULL;
	if (ready_queue_head)
	{
		thread = ready_queue_head;
		ready_queue_head = ready_queue_head->next;
		if (!ready_queue_head)
		{
			ready_queue_tail = NULL;
		}
	}

	spin_unlock(&scheduler_lock);
	return thread;
}

void scheduler_wake(thread_t* thread)
{
	if (thread == NULL)
	{
		return;
	}

	spin_lock(&scheduler_lock);

	if (thread->state == THREAD_BLOCKED && thread->context != NULL &&
		thread->blocked_rip >= 2)
	{
		thread->context->rip = thread->blocked_rip - 2;
		thread->context->rax = thread->blocked_rax;
		thread->blocked_rip = 0;
	}

	spin_unlock(&scheduler_lock);

	scheduler_enqueue(thread);
}

void scheduler_sleep_enqueue(thread_t* thread)
{
	thread->state = THREAD_BLOCKED;
	thread->next = sleep_queue_head;
	sleep_queue_head = thread;
}

void scheduler_check_sleeping_threads(void)
{
	thread_t* previous = NULL;
	thread_t* current = sleep_queue_head;

	while (current != NULL)
	{
		if (system_ticks >= current->sleep_until_ticks)
		{
			thread_t* next = current->next;
			if (previous == NULL)
			{
				sleep_queue_head = next;
			}
			else
			{
				previous->next = next;
			}

			scheduler_enqueue(current);
			serial_write("nanosleep: woken thread at tick ");
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


thread_t* scheduler_create_user_thread(void* entry_point, uint64_t pml4_physical, uint64_t user_rsp)
{
	uint64_t tcb_physical = physical_memory_manager_alloc_page();
	thread_t* thread = (thread_t*)physical_to_virtual(tcb_physical);

	uint64_t kstack_physical = physical_memory_manager_alloc_page();
	void* kstack_virtual = physical_to_virtual(kstack_physical);

	thread->kernel_stack_base = kstack_virtual;
	thread->page_directory = pml4_physical;
	thread->id = next_thread_id++;

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

	thread->context = ctx;
	thread->fds = fd_table_create_default();

	scheduler_enqueue(thread);
	return thread;
}

void scheduler_kill_current_thread(void)
{
	spin_lock(&scheduler_lock);

	current_thread->state = THREAD_DEAD;

	current_thread->next = dead_queue_head;
	dead_queue_head = current_thread;

	spin_unlock(&scheduler_lock);
}

void scheduler_reclaim_dead_threads(void)
{
	if (!dead_queue_head) return;

	spin_lock(&scheduler_lock);
	thread_t* thread = dead_queue_head;
	dead_queue_head = dead_queue_head->next;
	spin_unlock(&scheduler_lock);

	if (thread->fds != NULL)
	{
		fd_table_destroy(thread->fds);
		thread->fds = NULL;
	}

	uint64_t kstack_physical = VIRTUAL_TO_PHYSICAL(thread->kernel_stack_base);
	physical_memory_manager_free_page(kstack_physical);

	uint64_t tcb_physical = VIRTUAL_TO_PHYSICAL(thread);
	physical_memory_manager_free_page(tcb_physical);

	serial_write("Kernel resources of dead thread reclaimed.\r\n");
}

uint64_t schedule_callback(uint64_t old_rsp)
{
	if (!current_thread) return old_rsp;
	current_thread->context = (cpu_registers_t*)old_rsp;

	if (current_thread->state == THREAD_RUNNING && current_thread != idle_thread
	    && current_thread->kernel_stack_base != NULL)
	{
		scheduler_enqueue(current_thread);
	}

	thread_t* next_thread = scheduler_dequeue();
	if (!next_thread)
	{
		if(current_thread->state == THREAD_RUNNING)
		{
			next_thread = current_thread;
		}
		else
		{
			next_thread = idle_thread;
		}
	}

	thread_t* previous = current_thread;
	next_thread->state = THREAD_RUNNING;
	current_thread = next_thread;

	uint64_t active_cr3 = virtual_memory_manager_get_current_pml4();
	if (active_cr3 != next_thread->page_directory)
	{
		virtual_memory_manager_switch_address_space(next_thread->page_directory);
	}

	if (next_thread != previous)
	{
		tss_set_rsp0((uint64_t)next_thread->kernel_stack_base + 4096);

		uint64_t gs_base_address = read_msr(IA32_GS_BASE_MSR);
		if (gs_base_address != 0)
		{
			uint64_t* kernel_rsp_field = (uint64_t*)gs_base_address;
			*kernel_rsp_field = (uint64_t)next_thread->kernel_stack_base + 4096;
		}
	}

	return (uint64_t)next_thread->context;
}

bool scheduler_should_switch(void)
{
	thread_t* t = ready_queue_head;
	while (t != NULL)
	{
		if (t != current_thread)
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
	thread_t* boot_thread = (thread_t*)physical_to_virtual(tcb_physical);

	boot_thread->id = 0;
	boot_thread->state = THREAD_RUNNING;
	boot_thread->next = NULL;
	boot_thread->kernel_stack_base = NULL;
	boot_thread->page_directory = virtual_memory_manager_get_kernel_pml4();

	current_thread = boot_thread;

	static uint8_t idle_kstack[16384] __attribute__((aligned(16)));
	void* kstack_idle_virtual = idle_kstack;

	uint64_t tcb_idle_physical = physical_memory_manager_alloc_page();
	idle_thread = (thread_t*)physical_to_virtual(tcb_idle_physical);

	idle_thread->id = 0xFFFFFFFF;
	idle_thread->state = THREAD_READY;
	idle_thread->next = NULL;
	idle_thread->kernel_stack_base = kstack_idle_virtual;
	idle_thread->page_directory = virtual_memory_manager_get_kernel_pml4();

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

	idle_thread->context = ctx;

	boot_thread->fds = fd_table_create_default();

	serial_write("Scheduler initialized!\r\n");
}
