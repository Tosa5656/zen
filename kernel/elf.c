#include "elf.h"
#include "vfs.h"
#include "memory.h"
#include "scheduler.h"
#include "heap.h"
#include "serial.h"
#include <string.h>

static bool elf_validate_header(const Elf64_Executable_Header* header)
{
	if (header->e_ident[0] != 0x7F || header->e_ident[1] != 'E' || header->e_ident[2] != 'L' || header->e_ident[3] != 'F')
	{
		serial_write("File not ELF!");
		return false;
	}

	if (header->e_ident[4] != 2)
	{
		serial_write("File not for 64 bit!");
		return false;
	}
	if (header->e_machine != 0x3E)
	{
		serial_write("File not for x86_64!");
		return false;
	}
	if (header->e_type != 2)
	{
		serial_write("File not executable!");
		return false;
	}

	return true;
}


int elf_execute(const char* path)
{
	vfs_node_t* file_node = vfs_open(path, VFS_RDONLY);
	if (!file_node)
	{
		serial_write("File not found!");
		return -1;
	}

	uint64_t file_size = file_node->size;
	uint8_t* elf_buffer = (uint8_t*)kmalloc(file_size);
	if (!elf_buffer)
	{
		serial_write("Out of memory!");
		return -2;
	}

	int64_t read_bytes = vfs_read(file_node, 0, file_size, elf_buffer);
	vfs_close(file_node);

	if (read_bytes < (int64_t)sizeof(Elf64_Executable_Header))
	{
		serial_write("Failed to read file!");
		kfree(elf_buffer);
		return -3;
	}

	Elf64_Executable_Header* elf_header = (Elf64_Executable_Header*)elf_buffer;
	if (!elf_validate_header(elf_header))
	{
		serial_write("Failed to parse ELF header!");
		kfree(elf_buffer);
		return -4;
	}

	uint64_t entry_point = elf_header->e_entry;
	uint16_t program_header_count = elf_header->e_program_headers_number;
	uint64_t program_header_offset = elf_header->e_program_headers_offset;

	uint64_t user_pml4_physical = virtual_memory_manager_create_address_space();
	if (!user_pml4_physical)
	{
		serial_write("Failed to get virtual space for process!");
		kfree(elf_buffer);
		return -5;
	}

	Elf64_Program_Header* program_headers = (Elf64_Program_Header*)(elf_buffer + program_header_offset);

	uint64_t heap_start = 0;

	for (uint16_t i = 0; i < program_header_count; i++)
	{
		Elf64_Program_Header* program_header = &program_headers[i];

		// TODO: fully header parser
		if(program_header->p_type != 1)
		{
			continue;
		}

		uint64_t start_virtual = program_header->p_virtual_address & ~(PAGE_SIZE - 1);
		uint64_t end_virtual = (program_header->p_virtual_address + program_header->p_size_in_memory + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);
		uint64_t page_count = (end_virtual - start_virtual) / PAGE_SIZE;

		uint64_t page_flags = PAGE_PRESENT | PAGE_USER;
		if (program_header->p_flags & 2)
		{
			page_flags |= PAGE_WRITABLE;
		}

		for (uint64_t page = 0; page < page_count; page++)
		{
			uint64_t virtual_page = start_virtual + (page * PAGE_SIZE);
			uint64_t physical_frame = physical_memory_manager_alloc_page();

			virtual_memory_manager_map_user_page(user_pml4_physical, virtual_page, physical_frame, page_flags);

			void* virtual_kernel_ptr = physical_to_virtual(physical_frame);
			memset(virtual_kernel_ptr, 0, PAGE_SIZE);
		}

		if (end_virtual > heap_start)
		{
			heap_start = end_virtual;
		}

		uint64_t bytes_copied = 0;
		while (bytes_copied < program_header->p_size_in_file)
		{
			uint64_t current_virtual = program_header->p_virtual_address + bytes_copied;
			uint64_t physical_address = virtual_memory_manager_translate(user_pml4_physical, current_virtual);

			uint64_t page_offset = current_virtual & (PAGE_SIZE - 1);
			uint64_t space_in_page = PAGE_SIZE - page_offset;
			uint64_t chunk_size = program_header->p_size_in_file - bytes_copied;
			if (chunk_size > space_in_page)
			{
				chunk_size = space_in_page;
			}

			void* destination = (void*)((uintptr_t)physical_to_virtual(physical_address));
			void* source = (void*)(elf_buffer + program_header->p_offset + bytes_copied);

			memcpy(destination, source, chunk_size);
			bytes_copied += chunk_size;
		}
	}

	kfree(elf_buffer);

	uint64_t user_stack_top = USER_STACK_TOP;
	uint64_t user_stack_base = USER_STACK_BASE;

	for (uint64_t virtual_page = user_stack_base; virtual_page <= user_stack_top; virtual_page += PAGE_SIZE)
	{
		uint64_t physical_frame = physical_memory_manager_alloc_page();
		virtual_memory_manager_map_user_page(user_pml4_physical, virtual_page, physical_frame, PAGE_PRESENT | PAGE_WRITABLE | PAGE_USER);
		memset(physical_to_virtual(physical_frame), 0, PAGE_SIZE);
	}

	thread_t* user_thread = scheduler_create_user_thread(
		(void*)entry_point,
		user_pml4_physical,
		user_stack_top
	);

	if (!user_thread)
	{
		// TODO: memory clear if faield
		serial_write("Failed to create thread for executable!");
		return -6;
	}

	user_thread->brk_start = heap_start;
	user_thread->program_break = heap_start;

	return 0;
}
