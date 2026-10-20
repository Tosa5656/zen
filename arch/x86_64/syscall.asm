bits 64

global syscall_entry
extern syscall_dispatch
extern schedule_callback

CPU_LOCAL_KERNEL_RSP equ 0
CPU_LOCAL_USER_RSP equ 8

USER_CS equ 0x23 ; GDT_UCODE
USER_SS equ 0x1B ; GDT_UDATA

FRAME_RAX    equ 14 * 8
FRAME_RCX    equ 12 * 8
FRAME_R11    equ 4  * 8
FRAME_RIP    equ 17 * 8
FRAME_RFLAGS equ 19 * 8
FRAME_SIZE   equ 22 * 8

section .text
syscall_entry:
	swapgs
	cli
	mov [gs:CPU_LOCAL_USER_RSP], rsp
	mov rsp, [gs:CPU_LOCAL_KERNEL_RSP]

	push qword USER_SS
	push qword [gs:CPU_LOCAL_USER_RSP]
	push r11
	push qword USER_CS
	push rcx
	push qword 0
	push qword 0x80

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
	push r12
	push r13
	push r14
	push r15

	mov rdi, rsp
	cld
	call syscall_dispatch

	mov [rsp + 14*8], rax
	mov rdi, rsp
	call schedule_callback
	mov rsp, rax

	pop r15
	pop r14
	pop r13
	pop r12
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

	add rsp, 16

	pop rcx
	add rsp, 8
	pop r11

	pop qword [gs:CPU_LOCAL_USER_RSP]
	add rsp, 8

	mov rsp, [gs:CPU_LOCAL_USER_RSP]
	swapgs
	o64 sysret

global yield_iretq

yield_iretq:
	mov rsp, rdi
	pop r15
	pop r14
	pop r13
	pop r12
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
	add rsp, 16
	swapgs
	iretq

global context_iretq
context_iretq:
	mov  rsp, [rdi + 24]
	and  rsp, -16
	swapgs
	mov  rax, [rdi +  0]
	jmp  rax
