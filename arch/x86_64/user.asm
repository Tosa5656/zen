bits 64

global user_enter
user_enter:
	mov rsp, rdi
	iretq

global user_main_body
global user_main_end
user_main_body:
	mov rax, 1 ; SYSCALL_WRITE
	mov rdi, 1 ; stdout
	mov rsi, 0x600000+32 ; data msg1
	mov rdx, 49 ; length msg1
	syscall

	mov rax, 35 ; SYSCALL_NANOSLEEP
	mov rdi, 0x600000 ; req
	mov rsi, 0 ; rem
	syscall

	mov rax, 1 ; SYSCALL_WRITE
	mov rdi, 1 ; stdout
	mov rsi, 0x600000+128 ; data msg2
	mov rdx, 49 ; length msg2
	syscall

	mov rax, 60 ; SYSCALL_EXIT
	mov rdi, 7 ; return code
	syscall
	hlt
user_main_end: