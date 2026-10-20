bits 64

extern exception_handler
extern irq_handler
extern schedule_callback
extern scheduler_should_switch

global isr_ignored
global load_idt
global irq_common_stub

%macro ISR_NOERRCODE 1
global isr%1
isr%1:
	push qword 0
	push qword %1
	jmp isr_common_stub
%endmacro

%macro ISR_ERRCODE 1
global isr%1
isr%1:
	push qword %1
	jmp isr_common_stub
%endmacro

%macro IRQ 2
global irq%1
irq%1:
	push qword 0
	push qword %2
	jmp irq_common_stub
%endmacro

ISR_NOERRCODE 0  ; Divide by Zero
ISR_NOERRCODE 1  ; Debug
ISR_NOERRCODE 2  ; Non-Maskable Interrupt
ISR_NOERRCODE 3  ; Breakpoint
ISR_NOERRCODE 4  ; Overflow
ISR_NOERRCODE 5  ; Bound Range Exceeded
ISR_NOERRCODE 6  ; Invalid Opcode
ISR_NOERRCODE 7  ; Device Not Available
ISR_ERRCODE 8    ; Double Fault
ISR_NOERRCODE 9  ; Coprocessor Segment Overrun
ISR_ERRCODE 10   ; Invalid TSS
ISR_ERRCODE 11   ; Segment Not Present
ISR_ERRCODE 12   ; Stack-Segment Fault
ISR_ERRCODE 13   ; General Protection Fault (#GP)
ISR_ERRCODE 14   ; Page Fault (#PF)
ISR_NOERRCODE 15 ; Reserved
ISR_NOERRCODE 16 ; x87 Floating-Point Exception
ISR_ERRCODE 17   ; Alignment Check
ISR_NOERRCODE 18 ; Machine Check
ISR_NOERRCODE 19 ; SIMD Floating-Point Exception
ISR_NOERRCODE 20 ; Virtualization Exception
ISR_ERRCODE   21 ; Control Protection Exception

%assign i 22
%rep 10
	ISR_NOERRCODE i
%assign i i+1
%endrep

isr_common_stub:
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
	call exception_handler

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

	iretq

isr_ignored:
	iretq

load_idt:
	lidt [rdi]
	ret

IRQ 0,  32 ; Timer
IRQ 1,  33 ; Keyboard
IRQ 2,  34
IRQ 3,  35
IRQ 4,  36
IRQ 5,  37
IRQ 6,  38
IRQ 7,  39
IRQ 8,  40 ; Clock RTC
IRQ 9,  41
IRQ 10, 42
IRQ 11, 43
IRQ 12, 44 ; Mouse PS/2
IRQ 13, 45
IRQ 14, 46 ; ATA
IRQ 15, 47

irq_common_stub:
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
	call irq_handler

	call scheduler_should_switch
	test rax, rax
	jz .no_switch

	mov rdi, rsp
	call schedule_callback
	mov rsp, rax

.no_switch:
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
	iretq
