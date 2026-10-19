bits 64

global user_enter
user_enter:
	mov rsp, rdi
	iretq

global user_main_body
global user_main_end
user_main_body:
	mov rax, 1
	mov rdi, 1
	mov rsi, 0x600000
	mov rdx, 18
	syscall

	mov rax, 60
	mov rdi, 7
	syscall
	hlt
user_main_end: