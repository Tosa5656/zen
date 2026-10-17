%define FB_CONSOLE 1

bits 16
org 0x7E00

section .text
_start:
	mov si, boot_stage2_msg
	call print_string

	; Getting memory map
	mov si, get_memory_map_msg
	call print_string

	mov di, memory_map_buffer
	call get_memory_map

	test ax, ax
	jz .get_memory_map_failed

	movzx eax, ax
	mov [memory_map_entries_count], eax

	mov si, successfully_get_memory_map_msg
	call print_string

	; Init graphics
	mov si, init_graphics_msg
	call print_string

	mov ax, 1024
	mov bx, 768
	mov cl, 32
	mov si, vbe_buffer
	call init_graphics

	test ax, ax
	jz .init_graphics_failed

	; Set up the linear framebuffer text console
	call init_framebuffer
	call enable_unreal_mode

	mov si, successfully_init_graphics_msg
	call print_string_real_fb

	cli

	; Set up the GDT
	mov si, gdt_init_msg
	call print_string_real_fb

	; Enabling protected mode
	mov si, protected_mode_msg
	call print_string_real_fb

	mov ah, 0x03
	xor bh, bh
	int 0x10
	mov [cursor_row], dh

	; Read GPT table
	mov ah, 0x02
	mov al, 2 ; LBA 0 and LBA 1
	mov ch, 0
	mov cl, 1 ; LBA 0
	mov dh, 0
	mov dl, [0x500]	; boot disk
	mov bx, sector_lba0
	int 0x13
	jnc .gpt_read_ok
	jmp .halt
.get_memory_map_failed:
	mov si, failed_get_memory_map_msg
	call print_string
	jmp .halt
.init_graphics_failed:
	mov si, failed_init_graphics_msg
	call print_string
	jmp .halt
.halt:
	cli
	hlt
	jmp .halt
.gpt_read_ok:
	lgdt [gdt_descriptor]

	mov eax, cr0
	or eax, 1
	mov cr0, eax

	jmp 0x08:start_protected

section .bss
alignb 16
memory_map_buffer: resb 4096
memory_map_entries_count: resd 1
alignb 16
vbe_buffer: resb 512
alignb 16
sector_lba0: resb 1024
sector_lba1: equ sector_lba0 + 512
boot_info: resb 88

section .text

%INCLUDE "io_real.asm"
%INCLUDE "memory.asm"
%INCLUDE "graphics.asm"
%INCLUDE "framebuffer.asm"

%INCLUDE "io.asm"

bits 32

start_protected:
	mov ax, 0x10
	mov ds, ax
	mov es, ax
	mov fs, ax
	mov gs, ax
	mov ss, ax
	mov esp, 0x90000

	movzx edi, byte [cursor_row]
	imul edi, 160
	add edi, 0xB8000

	mov [video_cursor], edi

	mov ebx, successfully_protected_mode_msg
	call print_string_protected_fb

	; PAE Paging
	mov eax, 0x2003
	mov [0x1000], eax

	mov eax, 0x3003
	mov [0x2000], eax

	mov edi, 0x3000
	xor eax, eax
	mov ecx, 512
.map_low_memory:
	mov edx, eax
	or edx, 0x83
	mov [edi], edx
	add eax, 0x200000
	add edi, 8
	dec ecx
	jnz .map_low_memory

	mov edi, 0x4000
	xor eax, eax
	mov ecx, 1024
	rep stosd

	mov eax, 0x4003
	mov ecx, [fb_addr]
	shr ecx, 30
	mov [0x2000 + ecx*8], eax

	mov eax, [fb_addr]
	mov edx, eax
	shr edx, 21
	and edx, 0x1FF
	and eax, 0xFFE00000
	or eax, 0x83
	mov ecx, [fb_height]
	imul ecx, [fb_pitch]
	shr ecx, 21
	inc ecx
.map_fb_pages:
	mov [0x4000 + edx*8], eax
	add eax, 0x200000
	inc edx
	dec ecx
	jnz .map_fb_pages

	; Enabling PAE paging
	mov ebx, pae_paging_msg
	call print_string_protected_fb

	mov eax, 0x1000
	mov cr3, eax
	mov eax, cr4
	; PAE (bit 5) + OSFXSR (bit 9) + OSXMMEXCPT (bit 10) so that the SSE/SSE2
	; instructions the C compiler emits are usable in the kernel.
	or eax, (1 << 5) | (1 << 9) | (1 << 10)
	mov cr4, eax

	; Enabling long mode
	mov ebx, long_mode_msg
	call print_string_protected_fb

	mov ecx, 0xC0000080
	rdmsr
	or eax, 1 << 8
	wrmsr

	; Set up the 64-bit GDT
	lgdt [gdt64_descriptor]

	; Enable long mode (paging)
	mov eax, cr0
	or eax, 1 << 31
	mov cr0, eax

	jmp 0x08:start_long

bits 64
start_long:
	mov ax, 0x10
	mov ds, ax
	mov es, ax
	mov fs, ax
	mov gs, ax
	mov ss, ax

	mov esi, successfully_long_mode_msg
	call print_string_long_fb

	mov esi, setting_up_stack
	call print_string_long_fb

	mov rsp, stack_top

	; Find the GPT on the disk
	mov esi, gpt_check_msg
	call print_string_long_fb

	mov rdi, sector_lba0
	mov rsi, sector_lba1
	call check_gpt

	cmp rax, 1
	jne .gpt_not_found

	mov esi, successfully_gpt_check_msg
	call print_string_long_fb

	; Get partitions table info
	mov esi, gpt_extract_parameters_msg
	call print_string_long_fb

	mov rdi, sector_lba1
	mov rbx, rdi
	call gpt_extract_parameters

	cmp rax, 1
	jne .gpt_failed_extract_parameters

	mov esi, successfully_gpt_extract_parameters_msg
	call print_string_long_fb

	; Read GPT table
	mov esi, gpt_read_entries_msg
	call print_string_long_fb

	call gpt_read_entries
	cmp rax, 1
	jne .gpt_failed_read_entries

	mov esi, successfully_gpt_read_entries_msg
	call print_string_long_fb

	;Find EFI partition
	mov esi, gpt_find_efi_partition_msg
	call print_string_long_fb

	; ESP partition GUID
	mov rdi, 0x11D2F81FC12A7328
	mov rsi, 0x3BC93EC9A0004BBA
	mov rdx, efi_partition_start_lba
	mov rcx, efi_partition_end_lba

	call gpt_find_partition

	cmp rax, 1
	jne .partition_not_found

	mov esi, successfully_gpt_find_efi_partition_msg
	call print_string_long_fb

	mov esi, fat32_init_msg
	call print_string_long_fb

	mov rdi, [efi_partition_start_lba]
	call fat32_init
	cmp rax, 1
	jne .fat32_error

	mov esi, fat32_init_successfully_msg
        call print_string_long_fb

	mov esi, kernel_load_msg
	call print_string_long_fb

	mov rdi, 0x20204C454E52454B
	mov rsi, 0x004E4942
	mov rdx, 0x100000
	call fat32_read_file

	cmp rax, 1
	jne .kernel_load_error

	mov esi, kernel_load_successfully_msg
	call print_string_long_fb

	call find_system_tables

	call fill_boot_info

	mov rdi, boot_info
	jmp 0x100000
.gpt_not_found:
	mov esi, failed_gpt_check_msg
	call print_string_long_fb
	jmp .exit
.gpt_failed_extract_parameters:
	mov esi, failed_gpt_extract_paramters_msg
	call print_string_long_fb
	jmp .exit
.gpt_failed_read_entries:
	mov esi, failed_gpt_read_entries_msg
	call print_string_long_fb
	jmp .exit
.partition_not_found:
	mov esi, failed_gpt_find_efi_partition_msg
	call print_string_long_fb
	jmp .exit
.fat32_error:
        mov esi, fat32_init_failed_msg
        call print_string_long_fb
        jmp .exit
.kernel_load_error:
	mov esi, kernel_load_error_msg
	call print_string_long_fb
	jmp .exit
.exit:
	cli
	hlt
	jmp .exit
find_system_tables:
	push rbp
	mov rbp, rsp
	push rbx
	push rcx
	push rdx
	push rax

	movzx ebx, word [0x40E]
	shl rbx, 4
	test rbx, rbx
	jz .skip_ebda
	mov rdx, 0x000A0000
	call scan_system_table_region
.skip_ebda:
	mov rbx, 0x000E0000
	mov rdx, 0x00100000
	call scan_system_table_region

	pop rax
	pop rdx
	pop rcx
	pop rbx
	pop rbp
	ret

scan_system_table_region:
	cmp rbx, rdx
	jae .done

	mov rax, [rbx]
	mov rcx, 0x2052545020445352	; "RSD PTR "
	cmp rax, rcx
	jne .check_smbios
	mov [boot_info + BootInfo.acpi_rsdp_address], rbx
.check_smbios:
	mov eax, [rbx]
	and eax, 0x00FFFFFF
	cmp eax, 0x004D535F	; "_SM_"
	jne .next
	mov [boot_info + BootInfo.smbios_address], rbx
.next:
	add rbx, 16
	jmp scan_system_table_region
.done:
	ret

fill_boot_info:
	push rbp
	mov rbp, rsp
	push rax
	push rcx

	mov dword [boot_info + BootInfo.magic], 0xD480A6
	mov dword [boot_info + BootInfo.size], 88

	mov qword [boot_info + BootInfo.memory_map_address], memory_map_buffer
	mov eax, [memory_map_entries_count]
	mov [boot_info + BootInfo.memory_map_entries], rax

	mov eax, [vbe_buffer + 40]
	mov [boot_info + BootInfo.framebuffer_address], rax

	movzx eax, word [vbe_buffer + 18]
	mov [boot_info + BootInfo.framebuffer_width], eax

	movzx eax, word [vbe_buffer + 20]
	mov [boot_info + BootInfo.framebuffer_height], eax

	movzx eax, word [vbe_buffer + 16]
	mov [boot_info + BootInfo.framebuffer_pitch], eax

	mov al, [vbe_buffer + 25]
	mov [boot_info + BootInfo.framebuffer_bpp], al

	mov al, [vbe_buffer + 32]
	mov [boot_info + BootInfo.framebuffer_red_mask_shift], al

	mov al, [vbe_buffer + 34]
	mov [boot_info + BootInfo.framebuffer_green_mask_shift], al

	mov al, [vbe_buffer + 36]
	mov [boot_info + BootInfo.framebuffer_blue_mask_shift], al

	pop rcx
	pop rax
	pop rbp
	ret

%INCLUDE "gpt.asm"
%INCLUDE "fat32.asm"
%INCLUDE "boot_info.asm"

section .text

cursor_row: db 0
boot_stage2_msg: db "Bootloader stage 2 successfully loaded!", 0x0D, 0x0A, 0
get_memory_map_msg: db "Getting memory map from BIOS...", 0x0D, 0x0A, 0
successfully_get_memory_map_msg: db "Memory map successfully get from BIOS!", 0x0D, 0x0A, 0
failed_get_memory_map_msg: db "Failed to get memory map from BIOS!", 0x0D, 0x0A, 0
init_graphics_msg: db "Initing graphics...", 0x0D, 0x0A, 0
successfully_init_graphics_msg: db "Graphics successfully inited!", 0x0D, 0x0A, 0
failed_init_graphics_msg: db "Failed to init graphics!", 0x0D, 0x0A, 0
gdt_init_msg: db "Initializing the GDT...", 0x0D, 0x0A, 0
protected_mode_msg: db "Trying to enable protected mode...", 0x0D, 0x0A, 0
successfully_protected_mode_msg: db "Protected mode successfully enabled!", 0x0D, 0x0A, 0
pae_paging_msg: db "Enabling PAE paging...", 0x0D, 0x0A, 0
long_mode_msg: db "Trying to enable long mode...", 0x0D, 0x0A, 0
successfully_long_mode_msg: db "Long mode successfully enabled!", 0x0D, 0x0A, 0
setting_up_stack: db "Setting up a 16 KB stack...", 0x0D, 0x0A, 0
gpt_check_msg: db "Checking GPT on the disk...", 0x0D, 0x0A, 0
successfully_gpt_check_msg: db "GPT found on the disk!", 0x0D, 0x0A, 0
failed_gpt_check_msg: db "GPT not found on the disk!", 0x0D, 0x0A, 0
gpt_extract_parameters_msg: db "Trying extract GPT table params...", 0x0D, 0x0A, 0
successfully_gpt_extract_parameters_msg: db "GPT table params extracted successfully!", 0x0D, 0x0A, 0
failed_gpt_extract_paramters_msg: db "Failed extract GPT table params!", 0x0D, 0x0A, 0
gpt_read_entries_msg: db "Read GPT entries from disk...", 0x0D, 0x0A, 0
successfully_gpt_read_entries_msg: db "GPT entries successfully readed from disk!", 0x0D, 0x0A, 0
failed_gpt_read_entries_msg: db "Failed to read GPT entries from disk!", 0x0D, 0x0A, 0
gpt_find_efi_partition_msg: db "Find EFI partition in disk...", 0x0D, 0x0A, 0
successfully_gpt_find_efi_partition_msg: db "EFI partition successfully found!", 0x0D, 0x0A, 0
failed_gpt_find_efi_partition_msg: db "Failed to find EFI partitoin!", 0x0D, 0x0A, 0
fat32_init_msg: db "Trying init FAT32...", 0x0D, 0x0A, 0
fat32_init_failed_msg: db "Failed to init FAT32!", 0x0D, 0x0A, 0
fat32_init_successfully_msg: db "FAT32 inited successfully!", 0x0D, 0x0A, 0
kernel_load_msg: db "Reading kernel from disk...", 0x0D, 0x0A, 0
kernel_load_error_msg: db "Failed to read kernel from disk!", 0x0D, 0x0A, 0
kernel_load_successfully_msg: db "Kernel readed from disk successfully!", 0x0D, 0x0A, 0

gdt_start:
	dq 0
gdt_code:
	dw 0xFFFF
	dw 0x0
	db 0x0
	db 10011010b
	db 11001111b
	db 0x0
gdt_data:
	dw 0xffff
	dw 0x0
	db 0x0
	db 10010010b
	db 11001111b
	db 0x0
gdt_code16:
	dw 0xffff
	dw 0x0
	db 0x0
	db 10011010b
	db 00000000b
	db 0x0
gdt_end:
gdt_descriptor:
	dw gdt_end - gdt_start - 1
	dd gdt_start

align 8

gdt64_start:
	dq 0
gdt64_code:
	dq 0x00209A0000000000
gdt64_data:
	dq 0x0000920000000000
gdt64_end:
gdt64_descriptor:
	dw gdt64_end - gdt64_start - 1
	dd gdt64_start

%INCLUDE "font.asm"

times 10240-($-$$) db 0 ; 10 KB

section .bss
alignb 8
efi_partition_start_lba: resq 1
efi_partition_end_lba: resq 1
alignb 16
stack_bottom:
    resb 16384
stack_top:
