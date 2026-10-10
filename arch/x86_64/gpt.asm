%ifndef GPT_ASM_INC
%define GPT_ASM_INC
bits 64

global check_gpt
global gpt_extract_parameters

%INCLUDE "io.asm"

section .bss
align 8
gpt_partition_table_lba: resq 1
gpt_num_entries: resd 1
gpt_entry_size: resd 1
align 16
gpt_entries_buffer: resb 16384

section .text
struc GptHeader
    .signature: resq 1
    .revision: resd 1
    .header_size: resd 1
    .header_crc32: resd 1
    .reserved0: resd 1
    .current_lba: resq 1
    .backup_lba: resq 1
    .first_usable_lba: resq 1
    .last_usable_lba: resq 1
    .disk_guid: resb 16
    .partition_entry_lba: resq 1
    .num_partition_entries: resd 1
    .size_partition_entry: resd 1
    .partition_array_crc32: resd 1
endstruc

struc GptEntry
    .partition_type_guid: resb 16
    .unique_partition_guid: resb 16
    .starting_lba: resq 1
    .ending_lba: resq 1
    .attributes: resq 1
    .partition_name: resb 72
endstruc

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

gpt_extract_parameters:
	push rbp
	mov rbp, rsp

	; Get start LBA for patitions table
	mov r8, [rdi + GptHeader.partition_entry_lba]

	cmp r8, 2
	jb .invalid_header

	mov [gpt_partition_table_lba], r8

	; Get records count
	mov r9d, [rbx + GptHeader.num_partition_entries]

	test r9d, r9d
	jz .invalid_header

	mov [gpt_num_entries], r9d

	; Get record size
	mov r10d, [rbx + GptHeader.size_partition_entry]

	cmp r10d, 128
	jb .invalid_header

	mov [gpt_entry_size], r10d

	mov rax, 1
	jmp .exit
.invalid_header:
	mov rax, 0
.exit:
	pop rbp
	ret

gpt_read_entries:
	push rbp
	mov rbp, rsp
	push rbx

	mov eax, [gpt_num_entries]
	mov ecx, [gpt_entry_size]
	mul ecx

	shr eax, 9
	mov ebx, eax

	mov rdi, [gpt_partition_table_lba]
	mov rsi, rbx
	mov rdx, gpt_entries_buffer

	call read_sectors_long

	test rax, rax
	jz .error

	mov rax, 1
	jmp .exit
.error:
	xor rax, rax
.exit:
	pop rbx
	pop rbp
	ret

gpt_find_partition:
	push rbp
	mov rbp, rsp
	push rbx
	push r12
	push r13
	push r14
	push r15

	mov r14, rdx
	mov r15, rcx

	; Save addresses
	mov rbx, gpt_entries_buffer
	xor r12d, r12d
	mov r13d, [gpt_num_entries]
.loop:
	cmp r12d, r13d
	jge .not_found

	mov rax, [rbx + GptEntry.partition_type_guid]

	test rax, rax
	jz .next

	cmp rax, rdi
	jne .next

	mov rax, [rbx + GptEntry.partition_type_guid + 8]
	cmp rax, rsi
	jne .next

	mov rax, [rbx + GptEntry.starting_lba]
	mov [r14], rax

	mov rax, [rbx + GptEntry.ending_lba]
	mov [r15], rax

	mov rax, 1
	jmp .exit
.next:
	movzx eax, word [gpt_entry_size]
	add rbx, rax

	inc r12d
	jmp .loop
.not_found:
	xor rax, rax
.exit:
	pop r15
	pop r14
	pop r13
	pop r12
	pop rbx
	pop rbp
	ret

%endif
