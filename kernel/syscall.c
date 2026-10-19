#include "syscall.h"
#include "syscalls.h"
#include "msr.h"
#include "cpu.h"
#include "memory.h"
#include "panic.h"
#include "serial.h"
#include "keyboard.h"
#include "gdt.h"
#include "scheduler.h"
#include "time.h"

#define TIMER_HZ 100
#define MS_PER_TICK (1000 / TIMER_HZ)

extern void syscall_entry(void);
extern __attribute__((noreturn)) void context_iretq(uint64_t *frame);
extern thread_t* keyboard_waiting_thread;
extern thread_t* current_thread;
extern volatile uint64_t system_ticks;

typedef uint64_t (*syscall_fn_t)(cpu_registers_t *registers);

static uint64_t page_flags_at(uint64_t pml4_physical, uint64_t virt)
{
	size_t pml4_i = (virt >> 39) & 0x1FF;
	size_t pdpt_i = (virt >> 30) & 0x1FF;
	size_t pd_i   = (virt >> 21) & 0x1FF;
	size_t pt_i   = (virt >> 12) & 0x1FF;

	uint64_t *t = physical_to_virtual(pml4_physical);

	if (!(t[pml4_i] & PAGE_PRESENT))
		return 0;
	uint64_t *pdpt = physical_to_virtual(t[pml4_i] & ~0xFFFULL);

	if (!(pdpt[pdpt_i] & PAGE_PRESENT))
		return 0;
	uint64_t *pd = physical_to_virtual(pdpt[pdpt_i] & ~0xFFFULL);

	if (pd[pd_i] & PAGE_HUGE)
		return pd[pd_i];

	if (!(pd[pd_i] & PAGE_PRESENT))
		return 0;
	uint64_t *pt = physical_to_virtual(pd[pd_i] & ~0xFFFULL);

	return pt[pt_i];
}

static int user_range_ok(uint64_t ptr, uint64_t len, int writable)
{
	if (len == 0)
		return 1;
	if (ptr >= USER_SPACE_END)
		return 0;
	if (len > USER_SPACE_END - ptr)
		return 0;

	uint64_t pml4 = virtual_memory_manager_get_current_pml4();
	uint64_t need = PAGE_PRESENT | PAGE_USER;

	if (writable)
		need |= PAGE_WRITABLE;

	for (uint64_t address = ptr & ~(PAGE_SIZE - 1ULL); address < ptr + len; address += PAGE_SIZE)
	{
		if ((page_flags_at(pml4, address) & need) != need)
			return 0;
	}

	return 1;
}

static uint64_t sys_write(cpu_registers_t *registers)
{
	uint64_t fd = registers->rdi;
	uint64_t buf = registers->rsi;
	uint64_t count = registers->rdx;

	if (fd != 1 && fd != 2)
		return (uint64_t)-1;

	if (!user_range_ok(buf, count, 0))
		return (uint64_t)-1;

	const char *text = (const char *)buf;

	for (uint64_t i = 0; i < count; i++)
		serial_putc(text[i]);

	return count;
}

static uint64_t sys_read(cpu_registers_t *registers)
{
	uint64_t fd = registers->rdi;
	uint8_t *buf = (uint8_t *)registers->rsi;
	uint64_t count = registers->rdx;

	if (fd != 0)
		return (uint64_t)-1;

	if (!user_range_ok((uint64_t)buf, count, 1))
		return (uint64_t)-1;

	uint64_t got = 0;

	while (got < count && keyboard_pop(&buf[got]))
	{
		got++;
	}

	if (got > 0 || count == 0)
	{
		return got;
	}

	__asm__ volatile("cli");

	if (keyboard_pop(&buf[got]))
	{
		__asm__ volatile("sti");
		return 1;
	}

	keyboard_waiting_thread = current_thread;

	current_thread->state = THREAD_BLOCKED;

	uint64_t new_rsp = schedule_callback((uint64_t)registers);

	__asm__ volatile("mov %0, %%rsp" :: "r"(new_rsp));
	__asm__ volatile("sti");

	if (keyboard_pop(&buf[got]))
	{
		got++;
	}

	return got;

	return got;
}

static uint64_t sys_nanosleep(cpu_registers_t *registers)
{
	uint64_t req_user_address = registers->rdi;
	uint64_t rem_user_address = registers->rsi;

	if (!user_range_ok(req_user_address, sizeof(struct timespec), 0))
		return (uint64_t)-1;

	struct timespec* req = (struct timespec*)req_user_address;

	if (req->tv_nsec < 0 || req->tv_nsec >= 1000000000L)
		return (uint64_t)-1;

	uint64_t ticks_to_sleep = (req->tv_sec * TIMER_HZ) + (req->tv_nsec / (1000000L * MS_PER_TICK));

	if (ticks_to_sleep == 0)
	{
		return 0;
	}

	__asm__ volatile("cli");

	current_thread->sleep_until_ticks = system_ticks + ticks_to_sleep;

	extern void scheduler_sleep_enqueue(thread_t* thread);
	scheduler_sleep_enqueue(current_thread);

	uint64_t new_rsp = schedule_callback((uint64_t)registers);

	__asm__ volatile("mov %0, %%rsp" :: "r"(new_rsp));
	__asm__ volatile("sti");

	if (rem_user_address != 0 && user_range_ok(rem_user_address, sizeof(struct timespec), 1))
	{
		struct timespec* rem = (struct timespec*)rem_user_address;
		rem->tv_sec = 0;
		rem->tv_nsec = 0;
	}

	return 0;
}

static uint64_t sys_exit(cpu_registers_t *registers)
{
	uint64_t code = registers->rdi;

	serial_field("Process exited with code", code);

	if (cpu_current()->kernel_rip == 0 || cpu_current()->kernel_saved_rsp == 0)
		kernel_panic("syscall: exit without saved kernel context");

	virtual_memory_manager_switch_address_space(virtual_memory_manager_get_kernel_pml4());

	uint64_t frame[5];

	frame[0] = cpu_current()->kernel_rip;
	frame[1] = GDT_KCODE;
	frame[2] = 0x202;
	frame[3] = cpu_current()->kernel_saved_rsp;
	frame[4] = GDT_KDATA;

	context_iretq(frame);
}

static const syscall_fn_t syscall_table[SYSCALL_MAX + 1] =
{
	[SYSCALL_READ]     = sys_read,
	[SYSCALL_WRITE]    = sys_write,
	[SYSCALL_NAONSLEEP] = sys_nanosleep,
	[SYSCALL_EXIT]     = sys_exit,
};

void syscall_init(void)
{
	write_msr(IA32_EFER_MSR, read_msr(IA32_EFER_MSR) | EFER_SCE);
	write_msr(IA32_STAR_MSR, (0x10ULL << 48) | (0x08ULL << 32));
	write_msr(IA32_FMASK_MSR, 0x47700);
	write_msr(IA32_GS_BASE_MSR, 0);
	write_msr(IA32_KERNEL_GS_BASE_MSR, (uint64_t)&cpu_locals[0]);

	write_msr(IA32_LSTAR_MSR, (uint64_t)syscall_entry);

	serial_field("Syscalls: LSTAR", read_msr(IA32_LSTAR_MSR));
}

uint64_t syscall_dispatch(cpu_registers_t *registers)
{
	uint64_t number = registers->rax;

	if (number > SYSCALL_MAX)
		return (uint64_t)-1;

	if (registers->rip >= USER_SPACE_END)
		return (uint64_t)-1;

	if (registers->rsp >= USER_SPACE_END)
		return (uint64_t)-1;

	syscall_fn_t fn = syscall_table[number];

	if (fn == 0)
		return (uint64_t)-1;

	return fn(registers);
}
