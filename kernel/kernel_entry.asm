bits 64

global _start

extern kmain

KERNEL_VIRTUAL_BASE equ 0xFFFFFFFF80000000

PAGE_PRESENT equ (1 << 0)
PAGE_WRITE   equ (1 << 1)
PAGE_HUGE    equ (1 << 7)
HUGE_2MIB    equ (1 << 21)

section .bss
align 4096
boot_pml4: resb 4096
boot_pdpt: resb 4096
boot_pd:   resb 4096

align 16
boot_stack: resb 32768
boot_stack_top:

section .text

_start:
	mov rbx, rdi

	lea rsp, [rel boot_stack_top]

	lea rdi, [rel boot_pml4]
	xor eax, eax
	mov rcx, (4096 * 3) / 8
	rep stosq

	lea rdi, [rel boot_pd]
	xor eax, eax
	mov edx, PAGE_PRESENT | PAGE_WRITE | PAGE_HUGE
.fill_pd:
	mov [rdi + rax * 8], rdx
	add rdx, HUGE_2MIB
	inc rax
	cmp rax, 512
	jb .fill_pd

	lea rax, [rel boot_pdpt]
	mov edx, PAGE_PRESENT | PAGE_WRITE
	or rax, rdx
	lea rcx, [rel boot_pml4]
	mov [rcx], rax
	mov [rcx + 511 * 8], rax

	lea rax, [rel boot_pd]
	mov edx, PAGE_PRESENT | PAGE_WRITE
	or rax, rdx
	lea rcx, [rel boot_pdpt]
	mov [rcx], rax
	mov [rcx + 8], rax
	mov [rcx + 16], rax
	mov [rcx + 24], rax
	mov [rcx + 510 * 8], rax

	lea rax, [rel boot_pml4]
	mov cr3, rax

	mov rax, _start_high
	jmp rax

_start_high:
	lea rsp, [rel boot_stack_top]

	mov rdi, rbx
	call kmain

.halt:
	hlt
	jmp .halt
