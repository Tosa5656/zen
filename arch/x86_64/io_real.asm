bits 16

print_string:
	mov ah, 0x0E
	mov bh, 0
	mov bl, 0x07
.loop:
	lodsb
	cmp al, 0
	je .done

	int 0x10
	jmp .loop
.done:
	ret

%ifdef FB_CONSOLE
; input: si - string
print_string_real_fb:
	push ax
	push si
	cld
.loop:
	lodsb
	test al, al
	jz .done
	call fb_putc_real
	jmp .loop
.done:
	pop si
	pop ax
	ret

fb_putc_real:
	push eax
	push ebx
	push ecx
	push edx
	push esi
	push edi
	push ebp

	cmp al, 0x0D
	je .cr
	cmp al, 0x0A
	je .lf

	movzx ebx, al
	shl ebx, 4
	add ebx, font_bitmap

	mov eax, [fb_cursor_y]
	shl eax, 4
	imul eax, [fb_pitch]
	mov edx, [fb_cursor_x]
	shl edx, 3
	movzx ecx, byte [fb_bpp_bytes]
	imul edx, ecx
	add eax, edx
	add eax, [fb_addr]
	mov esi, eax

	mov edx, 16
.row:
	mov cl, [ebx]
	inc ebx
	mov edi, esi
	mov ebp, 8
.col:
	mov eax, [fb_bg]
	test cl, 0x80
	jz .have_colour
	mov eax, [fb_fg]
.have_colour:
	push edx
	movzx edx, byte [fb_bpp_bytes]
.write:
	mov [fs:edi], al
	shr eax, 8
	inc edi
	dec edx
	jnz .write
	pop edx
	shl cl, 1
	dec ebp
	jnz .col
	add esi, [fb_pitch]
	dec edx
	jnz .row

	inc dword [fb_cursor_x]
	call fb_wrap_real
	jmp .done
.cr:
	mov dword [fb_cursor_x], 0
	jmp .done
.lf:
	mov dword [fb_cursor_x], 0
	inc dword [fb_cursor_y]
	call fb_scroll_real
.done:
	pop ebp
	pop edi
	pop esi
	pop edx
	pop ecx
	pop ebx
	pop eax
	ret

fb_wrap_real:
	mov eax, [fb_width]
	xor edx, edx
	mov ecx, 8
	div ecx
	cmp [fb_cursor_x], eax
	jb .ok
	mov dword [fb_cursor_x], 0
	inc dword [fb_cursor_y]
	call fb_scroll_real
.ok:
	ret

fb_scroll_real:
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
	a32 rep movsb

	mov edi, [fb_addr]
	mov eax, [fb_height]
	sub eax, 16
	imul eax, [fb_pitch]
	add edi, eax
	mov ecx, [fb_pitch]
	shl ecx, 4
	xor eax, eax
	a32 rep stosb
.ok:
	ret
%endif

bits 16
