bits 16
org 0x7C00

global _start
global clear_screen
global read_bootloader_partition

section .text
	BOOTLOADER_PARTITION_SIZE: equ 1 ; stage2 size in sectors

_start:
	xor ax, ax
	mov ds, ax
	mov es, ax
	mov ss, ax
	mov sp, 0x7C00

	mov [boot_disk], dl

        call clear_screen

        call read_bootloader_partition

	jmp 0x7E00

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
read_bootloader_partition:
	pusha

	mov ah, 0x02
	mov al, BOOTLOADER_PARTITION_SIZE
	mov ch, 0x00
	mov cl, 0x02
	mov dh, 0x00
	mov dl, [boot_disk]

	mov bx, 0x7E00

	int 0x13
	jc disk_error
success:
	mov ah, 0x0E
	mov al, 'R'
	int 0x10

	popa
	ret
disk_error:
	mov ah, 0x0E
	mov al, 'E'
	int 0x10

	cli
	hlt

boot_disk: db 0x80
times 510-($-$$) db 0
dw 0xAA55
