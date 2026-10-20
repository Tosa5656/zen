bits 64

global spinlock_acquire
global spinlock_release

section .text
spinlock_acquire:
	mov eax, 1
.retry:
	xchg [rdi], eax

	test eax, eax
	jz .acquired
.spin:
	pause
	cmp dword [rdi], 0
	jne .spin
	mov eax, 1
	jmp .retry
.acquired:
	ret

spinlock_release:
	mov dword [rdi], 0
	ret
