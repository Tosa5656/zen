#include "usertest.h"
#include "memory.h"
#include "cpu.h"
#include "gdt.h"
#include "serial.h"
#include "scheduler.h"

extern const uint8_t user_main_body[];
extern const uint8_t user_main_end[];
extern void user_enter(uint64_t *frame);

#define USER_CODE      0x400000ULL
#define USER_DATA      0x600000ULL
#define USER_STACK_END 0x800000ULL

static const char user_message[] = "Hello from ring 3!";

static void usermode_after(void)
{
	serial_write("Back in kernel: EXIT syscall worked!\r\n");
	for (;;) {}
}

void usermode_test(void)
{
	uint64_t code_phys = physical_memory_manager_alloc_page();
	uint64_t data_phys = physical_memory_manager_alloc_page();
	uint64_t stack_phys = physical_memory_manager_alloc_page();
	uint64_t ret_phys = physical_memory_manager_alloc_page();
	uint64_t pml4 = virtual_memory_manager_create_address_space();

	if (code_phys == 0 || data_phys == 0 || stack_phys == 0 || ret_phys == 0 || pml4 == 0)
	{
		serial_write("usermode_test: allocation failed\r\n");
		return;
	}

	uint64_t flags = PAGE_PRESENT | PAGE_WRITABLE | PAGE_USER;

	if (virtual_memory_manager_map_user_page(pml4, USER_CODE, code_phys, flags) == 0 ||
		virtual_memory_manager_map_user_page(pml4, USER_DATA, data_phys, flags) == 0 ||
		virtual_memory_manager_map_user_page(pml4, USER_STACK_END - PAGE_SIZE, stack_phys, flags) == 0)
	{
		serial_write("usermode_test: map_user_page failed\r\n");
		return;
	}

	size_t code_size = user_main_end - user_main_body;

	uint8_t *code_dst = physical_to_virtual(code_phys);
	for (size_t i = 0; i < code_size; i++)
	{
		code_dst[i] = user_main_body[i];
	}

	uint8_t *data_dst = physical_to_virtual(data_phys);
	for (size_t i = 0; i < sizeof user_message; i++)
	{
		data_dst[i] = (uint8_t)user_message[i];
	}

	serial_field("usermode code size", code_size);

	cpu_current()->kernel_rip = (uint64_t)usermode_after;
	cpu_current()->kernel_saved_rsp = (uint64_t)physical_to_virtual(ret_phys) + PAGE_SIZE;

	scheduler_create_user_thread((void*)USER_CODE, pml4, USER_STACK_END);

	serial_write("usermode task added to ready queue.\r\n");
}
