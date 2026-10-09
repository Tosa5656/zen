bits 64

global _start

extern kmain

section .text
_start:
	call kmain
.halt:
	hlt
	jmp .halt
