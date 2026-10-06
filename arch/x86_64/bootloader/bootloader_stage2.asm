bits 16
org 0x7E00

global _start

section .text
_start:
	mov ah, 0x0E
	mov al, 'A'
	int 0x10

	times 512-($-$$) db 0
