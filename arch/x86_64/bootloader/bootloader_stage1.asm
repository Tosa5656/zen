bits 16
org 0x7C00

section .text
_start:
	xor ax, ax
	mov ds, ax
	mov es, ax
	mov ss, ax
	mov sp, 0x7C00

	cld

	mov [boot_disk], dl
	mov [0x500], dl ; Boot disk address for stage2

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

%INCLUDE "io_real.asm"
bits 16

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

	mov dl, [boot_disk]
	mov si, dap
	mov ah, 0x42
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

BOOTLOADER_PARTITION_SIZE: equ 20 ; stage2 size in sectors
STAGE2_LBA: equ 64 ; stage2 location (GPT entries area)
boot_disk: db 0x80
dap:
	db 0x10	; DAP size
	db 0 ; reserved
	dw BOOTLOADER_PARTITION_SIZE ; sectors to read
	dw 0x7E00 ; buffer offset
	dw 0 ; buffer segment
	dq STAGE2_LBA	; starting LBA
boot_msg: db "Booting ZenOS...", 0x0D, 0x0A, 0
disk_read_msg: db "Reading bootloader stage 2 from disk...", 0x0D, 0x0A, 0
successfully_disk_read_msg: db "Bootloader stage 2 successfully read from disk. Jumping...", 0x0D, 0x0A, 0
failed_disk_read_msg: db "Failed to read sectors from disk", 0x0D, 0x0A, 0

times 446-($-$$) db 0
