#include "syscall.h"
#include <sys/syscalls.h>
#include "msr.h"
#include "cpu.h"
#include "memory.h"
#include "panic.h"
#include "serial.h"
#include "keyboard.h"
#include "gdt.h"
#include "scheduler.h"
#include "time.h"
#include "timer.h"
#include "fd.h"
#include <string.h>
#include <fcntl.h>

#define TIMER_HZ SYSTEM_TICK_HZ
#define MS_PER_TICK (1000 / TIMER_HZ)

static uint32_t sys_open_access(uint64_t flags)
{
	uint32_t access = 0;

	switch (flags & O_ACCMODE)
	{
	case O_RDONLY:
		access = VFS_RDONLY;
		break;
	case O_WRONLY:
		access = VFS_WRONLY;
		break;
	case O_RDWR:
		access = VFS_RDWR;
		break;
	default:
		break;
	}

	if (flags & O_CREAT)
	{
		access |= VFS_CREATE;
	}

	return access;
}

extern void syscall_entry(void);
extern __attribute__((noreturn)) void context_iretq(uint64_t *frame);
extern process_t* current_process;
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

	if (!user_range_ok(buf, count, 0))
		return (uint64_t)-1;

	return fd_write((int)fd, (const void*)buf, count);
}

static uint64_t sys_read(cpu_registers_t *registers)
{
	uint64_t fd = registers->rdi;
	uint8_t *buf = (uint8_t *)registers->rsi;
	uint64_t count = registers->rdx;

	if (!user_range_ok((uint64_t)buf, count, 1))
		return (uint64_t)-1;

	uint64_t got = fd_read((int)fd, buf, count);

	if (got != FD_WOULD_BLOCK)
	{
		return got;
	}

	process_t* self = current_process;
	if (self == NULL)
	{
		return (uint64_t)-1;
	}

	__asm__ volatile("cli");

	self->blocked_rip = registers->rip;
	self->blocked_rax = registers->rax;
	self->state = PROCESS_BLOCKED;
	fd_wait_add(self);

	uint64_t again = fd_read(fd, buf, count);

	if (again != FD_WOULD_BLOCK)
	{
		fd_wait_remove(self);
		self->state = PROCESS_RUNNING;
		self->blocked_rip = 0;
		__asm__ volatile("sti");
		return again;
	}

	extern void yield_iretq(uint64_t new_rsp);
	uint64_t new_rsp = schedule_callback((uint64_t)registers);
	yield_iretq(new_rsp);
	__builtin_unreachable();
}

static int sys_copy_path(uint64_t user_ptr, char* out, uint64_t out_size)
{
	for (uint64_t length = 0; length + 1 < out_size; length++)
	{
		if (!user_range_ok(user_ptr + length, 1, 0))
		{
			return -1;
		}

		char byte = ((const char*)user_ptr)[length];
		if (byte == '\0')
		{
			out[length] = '\0';
			return (length == 0) ? -1 : 0;
		}

		out[length] = byte;
	}

	return -1;
}

static uint64_t sys_open(cpu_registers_t *registers)
{
	uint64_t path_user = registers->rdi;
	uint64_t flags = registers->rsi;

	char path[VFS_NAMELENGTH];

	if (sys_copy_path(path_user, path, sizeof(path)) != 0)
	{
		return (uint64_t)-1;
	}

	return (uint64_t)fd_open(path, sys_open_access(flags));
}

static uint64_t sys_openat(cpu_registers_t *registers)
{
	int64_t dirfd = (int64_t)registers->rdi;
	uint64_t path_user = registers->rsi;
	uint64_t flags = registers->rdx;

	char path[VFS_NAMELENGTH];

	if (sys_copy_path(path_user, path, sizeof(path)) != 0)
	{
		return (uint64_t)-1;
	}

	if (path[0] != '/' && dirfd != AT_FDCWD)
	{
		return (uint64_t)-1;
	}

	return (uint64_t)fd_open(path, sys_open_access(flags));
}

static uint64_t sys_close(cpu_registers_t *registers)
{
	uint64_t fd = registers->rdi;

	if (fd <= FD_STDERR || fd >= FD_MAX)
	{
		return (uint64_t)-1;
	}

	return (fd_close((int)fd) == 0) ? 0 : (uint64_t)-1;
}

static uint64_t sys_brk_page_down(uint64_t address)
{
	return address & ~(uint64_t)(PAGE_SIZE - 1);
}

static uint64_t sys_brk_page_up(uint64_t address)
{
	return (address + PAGE_SIZE - 1) & ~(uint64_t)(PAGE_SIZE - 1);
}

static void sys_brk_release_page(uint64_t pml4_physical, uint64_t page)
{
	uint64_t physical = virtual_memory_manager_translate(pml4_physical, page);
	if (physical == 0)
	{
		return;
	}

	virtual_memory_manager_unmap_user_page(pml4_physical, page);
	physical_memory_manager_free_page(physical);
}

static uint64_t sys_brk_reserve_pages(process_t* self, uint64_t from, uint64_t to)
{
	for (uint64_t page = from; page < to; page += PAGE_SIZE)
	{
		if (virtual_memory_manager_translate(self->page_directory, page) != 0)
		{
			continue;
		}

		uint64_t physical = physical_memory_manager_alloc_page();
		if (physical == 0)
		{
			while (page > from)
			{
				page -= PAGE_SIZE;
				sys_brk_release_page(self->page_directory, page);
			}

			return 0;
		}

		memset(physical_to_virtual(physical), 0, PAGE_SIZE);
		virtual_memory_manager_map_user_page(self->page_directory, page, physical,
			PAGE_PRESENT | PAGE_WRITABLE | PAGE_USER);
	}

	return 1;
}

static uint64_t sys_brk(cpu_registers_t* registers)
{
	uint64_t requested = registers->rdi;
	process_t* self = current_process;

	if (self == NULL || self->page_directory == 0 || self->brk_start == 0)
	{
		return (uint64_t)-1;
	}

	uint64_t current = self->program_break;

	if (requested == 0)
	{
		return current;
	}

	if (requested < self->brk_start || requested >= USER_BRK_LIMIT)
	{
		return current;
	}

	if (requested == current)
	{
		return current;
	}

	if (requested > current)
	{
		if (sys_brk_reserve_pages(self, sys_brk_page_up(current), sys_brk_page_up(requested)) == 0)
		{
			return current;
		}

		self->program_break = requested;
		return requested;
	}

	uint64_t page = sys_brk_page_up(requested);
	uint64_t end = sys_brk_page_down(current);

	while (page < end)
	{
		sys_brk_release_page(self->page_directory, page);
		page += PAGE_SIZE;
	}

	self->program_break = requested;
	return requested;
}

static uint64_t sys_dup(cpu_registers_t *registers)
{
	uint64_t fd = registers->rdi;

	if (fd >= FD_MAX)
	{
		return (uint64_t)-1;
	}

	return (uint64_t)fd_dup((int)fd);
}

static uint64_t sys_fcntl(cpu_registers_t *registers)
{
	uint64_t fd = registers->rdi;
	uint64_t cmd = registers->rsi;
	uint64_t arg = registers->rdx;

	if (fd >= FD_MAX)
	{
		return (uint64_t)-1;
	}

	return (uint64_t)fd_fcntl((int)fd, (int)cmd, arg);
}

static uint64_t sys_seek(cpu_registers_t *registers)
{
	uint64_t fd = registers->rdi;
	int64_t offset = (int64_t)registers->rsi;
	uint64_t whence = registers->rdx;

	if (fd >= FD_MAX || whence > VFS_SEEK_END)
	{
		return (uint64_t)-1;
	}

	int64_t position = fd_seek((int)fd, offset, (int)whence);

	return (position >= 0) ? (uint64_t)position : (uint64_t)-1;
}

static uint64_t sys_mkdir(cpu_registers_t *registers)
{
	char path[VFS_NAMELENGTH];

	if (sys_copy_path(registers->rdi, path, sizeof(path)) != 0)
	{
		return (uint64_t)-1;
	}

	return (vfs_mkdir(path) == 0) ? 0 : (uint64_t)-1;
}

static uint64_t sys_rmdir(cpu_registers_t *registers)
{
	char path[VFS_NAMELENGTH];

	if (sys_copy_path(registers->rdi, path, sizeof(path)) != 0)
	{
		return (uint64_t)-1;
	}

	return (vfs_rmdir(path) == 0) ? 0 : (uint64_t)-1;
}

static uint64_t sys_unlink(cpu_registers_t *registers)
{
	char path[VFS_NAMELENGTH];

	if (sys_copy_path(registers->rdi, path, sizeof(path)) != 0)
	{
		return (uint64_t)-1;
	}

	return (vfs_unlink(path) == 0) ? 0 : (uint64_t)-1;
}

static uint64_t sys_nanosleep(cpu_registers_t *registers)
{
	uint64_t req_user_address = registers->rdi;

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

	current_process->sleep_until_ticks = system_ticks + ticks_to_sleep;
	serial_write("nanosleep: sleeping until tick ");
	serial_dec(current_process->sleep_until_ticks);
	serial_write(" (now ");
	serial_dec(system_ticks);
	serial_write(")\r\n");

	extern void scheduler_sleep_enqueue(process_t* process);
	scheduler_sleep_enqueue(current_process);

	extern void yield_iretq(uint64_t new_rsp);
	uint64_t new_rsp = schedule_callback((uint64_t)registers);
	yield_iretq(new_rsp);
	__builtin_unreachable();
}

static uint64_t sys_exit(cpu_registers_t *registers)
{
	uint64_t code = registers->rdi;

	serial_write("Process ");
	serial_dec(current_process->id);
	serial_write(" exited with code: ");
	serial_dec(code);
	serial_write("\r\n");

	current_process->exit_code = (int64_t)code;

	__asm__ volatile("cli");

	uint64_t user_pml4 = current_process->page_directory;
	virtual_memory_manager_destroy_address_space(user_pml4);

	virtual_memory_manager_switch_address_space(virtual_memory_manager_get_kernel_pml4());

	extern void process_kill_current(void);
	process_kill_current();

	uint64_t new_rsp = schedule_callback((uint64_t)registers);

	extern void yield_iretq(uint64_t new_rsp);
	yield_iretq(new_rsp);
	__builtin_unreachable();
}

static uint64_t sys_waitpid(cpu_registers_t *registers)
{
	int64_t pid = (int64_t)registers->rdi;
	int *status_user = (int *)registers->rsi;
	uint64_t options = registers->rdx;

	(void)options;

	if (!user_range_ok((uint64_t)status_user, sizeof(int), 1))
	{
		return (uint64_t)-1;
	}

	for (;;)
	{
		process_t* zombie = process_take_zombie(pid);

		if (zombie != NULL)
		{
			int64_t code = zombie->exit_code;
			uint64_t zombie_pid = zombie->id;
			process_release(zombie);

			if (status_user != NULL)
			{
				*status_user = (int)((code & 0xFF) << 8);
			}

			return zombie_pid;
		}

		if (options & 1) /* WNOHANG */
		{
			return 0;
		}

		process_t* self = current_process;
		if (self == NULL)
		{
			return (uint64_t)-1;
		}

		__asm__ volatile("cli");

		self->blocked_rip = registers->rip;
		self->blocked_rax = registers->rax;
		self->state = PROCESS_BLOCKED;
		process_wait_enqueue(self);

		uint64_t new_rsp = schedule_callback((uint64_t)registers);

		extern void yield_iretq(uint64_t new_rsp);
		yield_iretq(new_rsp);
		__builtin_unreachable();
	}
}

static uint64_t sys_getpid(cpu_registers_t *registers)
{
	(void)registers;
	return (current_process != NULL) ? current_process->id : 0;
}

static uint64_t sys_getppid(cpu_registers_t *registers)
{
	(void)registers;
	/* Иерархии процессов пока нет — все процессы «сироты» с родителем 0 (init). */
	return 0;
}

static const syscall_fn_t syscall_table[SYSCALL_MAX + 1] =
{
	[SYSCALL_READ]      = sys_read,
	[SYSCALL_WRITE]     = sys_write,
	[SYSCALL_OPEN]      = sys_open,
	[SYSCALL_CLOSE]     = sys_close,
	[SYSCALL_SEEK]      = sys_seek,
	[SYSCALL_BRK]       = sys_brk,
	[SYSCALL_DUP]       = sys_dup,
	[SYSCALL_FCNTL]     = sys_fcntl,
	[SYSCALL_OPENAT]    = sys_openat,
	[SYSCALL_NAONSLEEP] = sys_nanosleep,
	[SYSCALL_GETPID]    = sys_getpid,
	[SYSCALL_GETPPID]   = sys_getppid,
	[SYSCALL_WAITPID]   = sys_waitpid,
	[SYSCALL_EXIT]      = sys_exit,
	[SYSCALL_MKDIR]     = sys_mkdir,
	[SYSCALL_RMDIR]     = sys_rmdir,
	[SYSCALL_UNLINK]    = sys_unlink,
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
