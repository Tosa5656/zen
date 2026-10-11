%ifndef IO_ASM_INC
%define IO_ASM_INC

bits 32

section .text
print_string_protected:
	push eax
	push ebx
	push ecx
	push edx
	push ebp
	push esi
	push edi

	cld
	mov edi, [video_cursor]
.loop:
	mov al, [ebx]
	test al, al
	jz .done

	cmp al, 0x0D
	je .cr
	cmp al, 0x0A
	je .lf

	mov ah, 0x0F
	stosw
	inc ebx
	jmp .loop
.cr:
	inc ebx
	jmp .loop
.lf:
	inc ebx
	mov eax, edi
	sub eax, 0xB8000
	xor edx, edx
	mov ecx, 160
	div ecx
	inc eax
	mul ecx
	add eax, 0xB8000
	mov edi, eax
	jmp .loop
.done:
	mov [video_cursor], edi

	pop edi
	pop esi
	pop ebp
	pop edx
	pop ecx
	pop ebx
	pop eax
	ret

; input: ebx - string
print_string_protected_fb:
	push eax
	push ebx
	push ecx
	push edx
	push esi
	push edi
	push ebp
	cld
.loop:
	mov al, [ebx]
	test al, al
	jz .done
	inc ebx
	call fb_putc_protected
	jmp .loop
.done:
	pop ebp
	pop edi
	pop esi
	pop edx
	pop ecx
	pop ebx
	pop eax
	ret

fb_putc_protected:
	push eax
	push ecx
	push edx
	push esi
	push edi
	push ebp

	cmp al, 0x0D
	je .cr
	cmp al, 0x0A
	je .lf

	movzx eax, al
	shl eax, 4
	add eax, font_bitmap
	mov esi, eax

	mov eax, [fb_cursor_y]
	shl eax, 4
	imul eax, [fb_pitch]
	mov edx, [fb_cursor_x]
	shl edx, 3
	movzx ecx, byte [fb_bpp_bytes]
	imul edx, ecx
	add eax, edx
	add eax, [fb_addr]
	mov edi, eax

	mov edx, 16
.row:
	mov cl, [esi]
	inc esi
	mov ebp, edi
	mov byte [fb_col], 8
.col:
	mov eax, [fb_bg]
	test cl, 0x80
	jz .have_colour
	mov eax, [fb_fg]
.have_colour:
	push ecx
	movzx ecx, byte [fb_bpp_bytes]
.write:
	mov [ebp], al
	shr eax, 8
	inc ebp
	dec ecx
	jnz .write
	pop ecx
	shl cl, 1
	dec byte [fb_col]
	jnz .col
	add edi, [fb_pitch]
	dec edx
	jnz .row

	inc dword [fb_cursor_x]
	call fb_wrap_protected
	jmp .done
.cr:
	mov dword [fb_cursor_x], 0
	jmp .done
.lf:
	mov dword [fb_cursor_x], 0
	inc dword [fb_cursor_y]
	call fb_scroll_protected
.done:
	pop ebp
	pop edi
	pop esi
	pop edx
	pop ecx
	pop eax
	ret

fb_wrap_protected:
	mov eax, [fb_width]
	xor edx, edx
	mov ecx, 8
	div ecx
	cmp [fb_cursor_x], eax
	jb .ok
	mov dword [fb_cursor_x], 0
	inc dword [fb_cursor_y]
	call fb_scroll_protected
.ok:
	ret

fb_scroll_protected:
	mov eax, [fb_height]
	xor edx, edx
	mov ecx, 16
	div ecx
	cmp [fb_cursor_y], eax
	jb .ok
	dec dword [fb_cursor_y]

	mov edi, [fb_addr]
	mov esi, edi
	mov eax, [fb_pitch]
	shl eax, 4
	add esi, eax
	mov ecx, [fb_height]
	sub ecx, 16
	imul ecx, [fb_pitch]
	cld
	rep movsb

	mov edi, [fb_addr]
	mov eax, [fb_height]
	sub eax, 16
	imul eax, [fb_pitch]
	add edi, eax
	mov ecx, [fb_pitch]
	shl ecx, 4
	xor eax, eax
	rep stosb
.ok:
	ret

bits 64
default abs

print_string_long:
	push rax
	push rbx
	push rcx
	push rdx
	push rsi
	push rbp
	push rdi

	cld
	mov edi, [video_cursor]
.loop:
	mov al, [rsi]
	test al, al
	jz .done

	cmp al, 0x0D
	je .cr
	cmp al, 0x0A
	je .lf

	mov ah, 0x0F
	stosw
	inc rsi
	jmp .loop
.cr:
	inc rsi
	jmp .loop
.lf:
	inc rsi
	mov rax, rdi
	sub rax, 0xB8000
	xor rdx, rdx
	mov rcx, 160
	div rcx
	inc rax
	mul rcx
	add rax, 0xB8000
	mov rdi, rax
	jmp .loop
.done:
	mov [video_cursor], edi

	pop rdi
	pop rbp
	pop rsi
	pop rdx
	pop rcx
	pop rbx
	pop rax
	ret

; input: rsi - string
print_string_long_fb:
	push rax
	push rbx
	push rcx
	push rdx
	push rsi
	push rdi
	push rbp
	push r8
	push r9
	push r10
	push r11
	cld
.loop:
	mov al, [rsi]
	test al, al
	jz .done
	inc rsi
	call fb_putc_long
	jmp .loop
.done:
	pop r11
	pop r10
	pop r9
	pop r8
	pop rbp
	pop rdi
	pop rsi
	pop rdx
	pop rcx
	pop rbx
	pop rax
	ret

fb_putc_long:
	push rax
	push rcx
	push rdx
	push r8
	push r9
	push r10
	push r11

	cmp al, 0x0D
	je .cr
	cmp al, 0x0A
	je .lf

	movzx rax, al
	shl rax, 4
	add rax, font_bitmap
	mov r8, rax

	mov eax, [fb_cursor_y]
	shl rax, 4
	mov ecx, [fb_pitch]
	imul rax, rcx
	mov edx, [fb_cursor_x]
	shl rdx, 3
	movzx ecx, byte [fb_bpp_bytes]
	imul rdx, rcx
	add rax, rdx
	mov ecx, [fb_addr]
	add rax, rcx
	mov r9, rax

	mov r10d, 16
.row:
	mov cl, [r8]
	inc r8
	mov r11, r9
	mov byte [fb_col], 8
.col:
	mov eax, [fb_bg]
	test cl, 0x80
	jz .have_colour
	mov eax, [fb_fg]
.have_colour:
	push rcx
	movzx ecx, byte [fb_bpp_bytes]
.write:
	mov [r11], al
	shr eax, 8
	inc r11
	dec ecx
	jnz .write
	pop rcx
	shl cl, 1
	dec byte [fb_col]
	jnz .col
	mov eax, [fb_pitch]
	add r9, rax
	dec r10d
	jnz .row

	inc dword [fb_cursor_x]
	call fb_wrap_long
	jmp .done
.cr:
	mov dword [fb_cursor_x], 0
	jmp .done
.lf:
	mov dword [fb_cursor_x], 0
	inc dword [fb_cursor_y]
	call fb_scroll_long
.done:
	pop r11
	pop r10
	pop r9
	pop r8
	pop rdx
	pop rcx
	pop rax
	ret

fb_wrap_long:
	mov eax, [fb_width]
	xor edx, edx
	mov ecx, 8
	div ecx
	cmp [fb_cursor_x], eax
	jb .ok
	mov dword [fb_cursor_x], 0
	inc dword [fb_cursor_y]
	call fb_scroll_long
.ok:
	ret

fb_scroll_long:
	mov eax, [fb_height]
	xor edx, edx
	mov ecx, 16
	div ecx
	cmp [fb_cursor_y], eax
	jb .ok
	dec dword [fb_cursor_y]

	mov edi, [fb_addr]
	mov esi, edi
	mov eax, [fb_pitch]
	shl eax, 4
	add esi, eax
	mov ecx, [fb_height]
	sub ecx, 16
	imul ecx, [fb_pitch]
	cld
	rep movsb

	mov edi, [fb_addr]
	mov eax, [fb_height]
	sub eax, 16
	imul eax, [fb_pitch]
	add edi, eax
	mov ecx, [fb_pitch]
	shl ecx, 4
	xor eax, eax
	rep stosb
.ok:
	ret

read_sectors_long:
	push rbp
	mov rbp, rsp
	push rbx
	push rcx
	push rdx
	push rdi
	push rsi
	push r12
	push r13

	mov rbx, rdx
	mov r12, rdi
	mov r13d, esi

	mov dx, 0x1F7
.wait_ready1:
	in al, dx
	test al, 0x80
	jnz .wait_ready1

	mov dx, 0x1F6
	mov al, 0x40
	out dx, al

	mov dx, 0x1F2
	mov ax, r13w
	shr ax, 8
	out dx, al
	mov ax, r13w
	out dx, al

	mov dx, 0x1F3
	mov rcx, r12
	shr rcx, 24
	mov al, cl
	out dx, al

	mov dx, 0x1F4
	mov rcx, r12
	shr rcx, 32
	mov al, cl
	out dx, al

	mov dx, 0x1F5
	mov rcx, r12
	shr rcx, 40
	mov al, cl
	out dx, al

	mov dx, 0x1F3
	mov rcx, r12
	mov al, cl
	out dx, al

	mov dx, 0x1F4
	mov rcx, r12
	shr rcx, 8
	mov al, cl
	out dx, al

	mov dx, 0x1F5
	mov rcx, r12
	shr rcx, 16
	mov al, cl
	out dx, al

	mov dx, 0x1F7
	mov al, 0x24
	out dx, al

.next_sector:
	test r13d, r13d
	jz .read_done

	mov dx, 0x1F7
.wait_drq:
	in al, dx
	test al, 0x80
	jnz .wait_drq
	test al, 0x08
	jz .wait_drq

	mov dx, 0x1F0
	mov ecx, 256
.read_words:
	in ax, dx
	mov [rbx], ax
	add rbx, 2
	loop .read_words

	dec r13d
	jmp .next_sector

.read_done:
	mov rax, 1
	jmp .exit

.exit:
	pop r13
	pop r12
	pop rsi
	pop rdi
	pop rdx
	pop rcx
	pop rbx
	pop rbp
	ret

bits 16

video_cursor: dd 0xB8000
%endif
