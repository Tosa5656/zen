%ifndef GRAPHICS_ASM_INC
%define GRAPHICS_ASM_INC
bits 16

section .bss
align 16
vbe_info_block: resb 512
vbe_mode_info:  resb 512
gfx_width:      resw 1
gfx_height:     resw 1
gfx_bpp:        resb 1
gfx_buffer:     resw 1

section .text

; input: ax - width, bx - height, cx - bpp, si - buffer offset
; return: ax - result
init_graphics:
	push bp
	mov bp, sp
	push bx
	push cx
	push dx
	push si
	push di

	mov [gfx_width], ax
	mov [gfx_height], bx
	mov [gfx_bpp], cl
	mov [gfx_buffer], si

	mov ax, 0x4F00
	mov di, vbe_info_block
	int 0x10
	cmp ax, 0x004F
	jne .fail

	mov eax, [vbe_info_block]
	cmp eax, 0x41534556 ; "VESA"
	jne .fail

	mov si, [vbe_info_block + 14]
	mov ax, [vbe_info_block + 16]
	mov fs, ax
.mode_search_loop:
	mov cx, [fs:si]
	cmp cx, 0xFFFF
	je .fail

	add si, 2
	push si

	mov ax, 0x4F01
	mov di, vbe_mode_info
	int 0x10
	pop si
	cmp ax, 0x004F
	jne .mode_search_loop

	mov ax, [vbe_mode_info]
	and ax, 0x0091
	cmp ax, 0x0091
	jne .mode_search_loop

	mov ax, [vbe_mode_info + 18]
	cmp ax, [gfx_width]
	jne .mode_search_loop

	mov ax, [vbe_mode_info + 20]
	cmp ax, [gfx_height]
	jne .mode_search_loop

	mov al, [vbe_mode_info + 25]
	cmp al, [gfx_bpp]
	jne .mode_search_loop

	mov cx, [fs:si - 2]
	or cx, 0x4000
	mov ax, 0x4F02
	mov bx, cx
	int 0x10
	cmp ax, 0x004F
	jne .fail

	cld
	mov cx, 256
	mov si, vbe_mode_info
	mov di, [gfx_buffer]
	rep movsw

	mov ax, 1
	jmp .exit
.fail:
	xor ax, ax
.exit:
	pop di
	pop si
	pop dx
	pop cx
	pop bx
	pop bp
	ret

%endif
