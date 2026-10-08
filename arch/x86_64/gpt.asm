bits 64

global check_gpt

check_gpt:
	push rbp
	mov rbp, rsp

	; Check LBA 0
	mov ax, [rdi+510]
	cmp ax, 0xAA55

	mov al, [rdi + 450]
	cmp al, 0xEE
	jne .not_gpt

	; Check LBA 1
	mov rax, [rsi]
	mov rcx, 0x5452415020494645
	cmp rax, rcx
	jne .not_gpt

	; If all conditions are met => GPT
	mov rax, 1
	jmp .exit
.not_gpt:
	xor rax, rax
.exit:
	pop rbp
	ret
