#ifndef ELF_H
#define ELF_H

#include <stdint.h>

typedef uint64_t Elf64_Address;
typedef uint64_t Elf64_Offset;
typedef uint16_t Elf64_Half;
typedef uint32_t Elf64_Word;
typedef uint64_t Elf64_Xword;

typedef struct
{
	unsigned char e_ident[16];
	Elf64_Half e_type;
	Elf64_Half e_machine;
	Elf64_Word e_version;
	Elf64_Address e_entry;
	Elf64_Offset e_program_headers_offset;
	Elf64_Offset e_section_headers_offset;
	Elf64_Word e_flags;
	Elf64_Half e_executable_header_size;
	Elf64_Half e_program_headers_entry_size;
	Elf64_Half e_program_headers_number;
	Elf64_Half e_section_headers_entry_size;
	Elf64_Half e_section_headers_number;
	Elf64_Half e_section_headers_string_table_index;
} Elf64_Executable_Header;

typedef struct
{
	Elf64_Word p_type;
	Elf64_Word p_flags;
	Elf64_Offset p_offset;
	Elf64_Address p_virtual_address;
	Elf64_Address p_physical_address;
	Elf64_Xword p_size_in_file;
	Elf64_Xword p_size_in_memory;
	Elf64_Xword p_align;
} Elf64_Program_Header;

int elf_execute(const char* path);

#endif
