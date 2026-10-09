%ifndef IO_ASM_INC
%define IO_ASM_INC

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
