bits 16
org 0x7C00

global _start
global clear_screen

section .text
_start:
        call clear_screen

        mov ah, 0x0E
        mov al, 'B'
        int 0x10

        cli
        hlt
clear_screen:
	pusha

	mov ah, 0x06
	mov al, 0
	mov bh, 0x07
	mov ch, 0
	mov cl, 0
	mov dh, 24
	mov dl, 79
	int 0x10

	mov ah, 0x02
	mov bh, 0
	mov dh, 0
	mov dl, 0
	int 0x10

	popa
	ret

times 510-($-$$) db 0
dw 0xAA55
