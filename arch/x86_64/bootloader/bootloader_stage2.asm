bits 16
org 0x7E00

section .text
_start:
	mov si, boot_stage2_msg
	call print_string

	cli

	; Set GDT table
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

	mov eax, cr0
	or eax, 1
	mov cr0, eax

	jmp 0x08:start_protected

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

	; Enabling real mode
	mov ebx, real_mode_msg
	call print_string_protected

	mov ecx, 0xC0000080
	rdmsr
	or eax, 1 << 8
	wrmsr

	; Set 64-bit GDT table
	lgdt [gdt64_descriptor]

	; Enable compatibility mode
	mov eax, cr0
	or eax, 1 << 31
	mov cr0, eax

	jmp 0x08:start_real

bits 64
start_real:
	mov ax, 0x10
	mov ds, ax
	mov es, ax
	mov fs, ax
	mov gs, ax
	mov ss, ax

	mov esi, successfully_real_mode_msg
        call print_string_real

	mov esi, settings_up_stack
	call print_string_real

	mov rsp, stack_top

	hlt

cursor_row: db 0
boot_stage2_msg: db "Bootloader stage 2 successfully loaded!", 0x0D, 0x0A, 0
gdt_init_msg: db "Initializing GDT table...", 0x0D, 0x0A, 0
protected_mode_msg: db "Trying enable protected mode...", 0x0D, 0x0A, 0
successfully_protected_mode_msg: db "Protected mode successfuly enabled!", 0x0D, 0x0A, 0
pae_paging_msg: db "Enabling PAE paging...", 0x0D, 0x0A, 0
real_mode_msg: db "Trying enable real mode...", 0x0D, 0x0A, 0
settings_up_stack: db "Settings up 16 KB stack...", 0x0D, 0x0A, 0
successfully_real_mode_msg: db "Real mode successfuly enabled!", 0x0D, 0x0A, 0
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
stack_bottom:
    resb 16384
stack_top:
