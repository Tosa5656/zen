bits 16
org 0x7C00

global _start
global clear_screen
global read_bootloader_partition

section .text
_start:
	xor ax, ax
	mov ds, ax
	mov es, ax
	mov ss, ax
	mov sp, 0x7C00

	cld

	mov [boot_disk], dl

	call clear_screen

	; Boot start
	mov si, boot_msg
	call print_string

	call read_bootloader_partition

	mov si, successfully_disk_read_msg
	call print_string

	jmp 0x7E00

	cli
	hlt

%INCLUDE "io.asm"

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
	mov si, disk_read_msg
	call print_string

	popa
	ret
disk_error:
	mov si, failed_disk_read_msg
	call print_string

	cli
	hlt

BOOTLOADER_PARTITION_SIZE: equ 1 ; stage2 size in sectors
boot_disk: db 0x80
boot_msg: db "Booting ZenOS...", 0x0D, 0x0A, 0
disk_read_msg: db "Reading bootloader stage 2 from disk", 0x0D, 0x0A, 0
successfully_disk_read_msg: db "Bootloader stage 2 successfully readed from disk. Jumping...", 0x0D, 0x0A, 0
failed_disk_read_msg: db "Failed to read sectors from disk", 0x0D, 0x0A, 0

times 510-($-$$) db 0
dw 0xAA55
