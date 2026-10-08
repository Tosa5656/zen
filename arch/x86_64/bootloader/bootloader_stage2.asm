bits 16
org 0x7E00

section .text
_start:
	mov si, boot_stage2_msg
	call print_string

	cli

	; Set up the GDT
	mov si, gdt_init_msg
	call print_string

	lgdt [gdt_descriptor]

	; Enabling protected mode
	mov si, protected_mode_msg
	call print_string

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
	hlt
.gpt_read_ok:
	mov eax, cr0
	or eax, 1
	mov cr0, eax

	jmp 0x08:start_protected

%INCLUDE "io_real.asm"

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
	call print_string_protected

	; PAE Paging
	mov eax, 0x2003
	mov [0x1000], eax

	mov eax, 0x3003
	mov [0x2000], eax

	mov eax, 0x00000083
	mov [0x3000], eax

	; Enabling PAE paging
	mov ebx, pae_paging_msg
	call print_string_protected

	mov eax, 0x1000
	mov cr3, eax
	mov eax, cr4
	or eax, 1 << 5
	mov cr4, eax

	; Enabling long mode
	mov ebx, long_mode_msg
	call print_string_protected

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
	call print_string_long

	mov esi, setting_up_stack
	call print_string_long

	mov rsp, stack_top

	; Find the GPT on the disk
	mov esi, gpt_check_msg
	call print_string_long

	mov rdi, sector_lba0
	mov rsi, sector_lba1
	call check_gpt

	cmp rax, 1
	jne .gpt_not_found

	mov esi, successfully_gpt_check_msg
	call print_string_long

	jmp .exit
.gpt_not_found:
	mov esi, failed_gpt_check_msg
	call print_string_long
	jmp .exit
.exit:
	hlt

%INCLUDE "gpt.asm"

cursor_row: db 0
boot_stage2_msg: db "Bootloader stage 2 successfully loaded!", 0x0D, 0x0A, 0
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


times 10240-($-$$) db 0 ; 10 KB

section .bss
align 16
sector_lba0: resb 512
sector_lba1: resb 512
stack_bottom:
    resb 16384
stack_top:
