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

bits 32

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

bits 64
default abs

print_string_real:
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

bits 16

video_cursor: dd 0xB8000
