bits 64
section .text

global gdt_flush
global tss_flush

gdt_flush:
	lgdt [rdi]
	mov ax, 0x10
	mov ds, ax
	mov es, ax
	mov ss, ax
	xor eax, eax
	mov fs, ax
	mov gs, ax
	push qword 0x08
	lea rax, [rel .reload]
	push rax
	o64 retf
.reload:
	ret

tss_flush:
	mov ax, di
	ltr ax
	ret
