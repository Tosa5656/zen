%ifndef FAT32_ASM_INC
%define FAT32_ASM_INC

bits 64

section .bss
align 8
fat32_start_lba: resq 1
fat32_fat_lba: resq 1
fat32_data_lba: resq 1
fat32_reserved_sectors: resd 1
fat32_sectors_per_fat: resd 1
fat32_sec_per_cluster: resd 1
fat32_root_cluster: resd 1
kernel_current_cluster: resd 1
align 16
fat32_scratch_buffer: resb 4096

section .text
struc Fat32Vbr
	.jmp_code: resb 3
	.oem_name: resb 8
	.bytes_per_sector: resw 1
	.sectors_per_cluster: resb 1
	.reserved_sectors: resw 1
	.num_fats: resb 1
	.root_entries: resw 1
	.total_sectors_16: resw 1
	.media_type: resb 1
	.sectors_per_fat_16: resw 1
	.sectors_per_track: resw 1
	.num_heads: resw 1
	.hidden_sectors: resd 1
	.total_sectors_32: resd 1
	.sectors_per_fat_32: resd 1
	.flags:	resw 1
	.fat_version: resw 1
	.root_cluster: resd 1
endstruc

struc Fat32DirEntry
	.name: resb 11
	.attributes: resb 1
	.reserved: resb 1
	.creation_time_ms: resb 1
	.creation_time:	resw 1
	.creation_date:	resw 1
	.last_access_date: resw 1
	.first_cluster_high: resw 1
	.last_mod_time:	resw 1
	.last_mod_date:	resw 1
	.first_cluster_low: resw 1
	.file_size: resd 1
endstruc

section .text
; Input: RDI - disk LBA start
; Output: RAX - resul
fat32_init:
	push rbp
	mov rbp, rsp
	push rbx
	push rcx
	push rdx

	mov [fat32_start_lba], rdi

	mov rdi, [fat32_start_lba]
	mov rsi, 1
	mov rdx, fat32_scratch_buffer
	call read_sectors_long
	test rax, rax
	jz .error

	mov rbx, fat32_scratch_buffer

	movzx eax, byte [rbx + Fat32Vbr.sectors_per_cluster]
	mov [fat32_sec_per_cluster], eax

	movzx ecx, word [rbx + Fat32Vbr.reserved_sectors]
	mov [fat32_reserved_sectors], ecx

	mov eax, [rbx + Fat32Vbr.sectors_per_fat_32]
	mov [fat32_sectors_per_fat], eax

	mov eax, [rbx + Fat32Vbr.root_cluster]
	mov [fat32_root_cluster], eax

	mov rax, [fat32_start_lba]
	add rax, rcx
	mov [fat32_fat_lba], rax

	movzx ecx, byte [rbx + Fat32Vbr.num_fats]
	mov eax, [fat32_sectors_per_fat]
	mul ecx

	mov rdx, [fat32_fat_lba]
	add rdx, rax
	mov [fat32_data_lba], rdx

	mov rax, 1
	jmp .exit
.error:
	xor rax, rax
.exit:
	pop rdx
	pop rcx
	pop rbx
	pop rbp
	ret

fat32_read_file:
	push rbp
	mov rbp, rsp
	push rbx
	push r12
	push r13
	push r14
	push r15

	mov r12, rdi
	mov r14, rdx
	mov r15, rsi

	mov rdi, [fat32_root_cluster]
	call cluster_to_lba

	mov rdi, rax
	mov esi, [fat32_sec_per_cluster]
	mov rdx, fat32_scratch_buffer
	call read_sectors_long
	test rax, rax
	jz .error

	mov rbx, fat32_scratch_buffer
	xor r13d, r13d
.search_loop:
	cmp r13d, 128
	jge .not_found

	mov al, [rbx + Fat32DirEntry.name]
	test al, al
	jz .not_found

	mov rax, [rbx + Fat32DirEntry.name]
	cmp rax, r12
	jne .next_entry

	mov eax, [rbx + Fat32DirEntry.name + 8]
	and eax, 0x00FFFFFF
	mov ecx, r15d
	and ecx, 0x00FFFFFF
	cmp eax, ecx
	je .file_found
.next_entry:
	add rbx, 32
	inc r13d
	jmp .search_loop
.file_found:
	movzx ecx, word [rbx + Fat32DirEntry.first_cluster_high]
	shl ecx, 16
	mov cx, [rbx + Fat32DirEntry.first_cluster_low]
	mov [kernel_current_cluster], ecx
.load_file_loop:
	mov rdi, [kernel_current_cluster]
	call cluster_to_lba

	mov rdi, rax
	mov esi, [fat32_sec_per_cluster]
	mov rdx, r14
	call read_sectors_long
	test rax, rax
	jz .error

	mov eax, [fat32_sec_per_cluster]
	shl eax, 9
	add r14, rax

	mov eax, [kernel_current_cluster]
	shl eax, 2

	mov ecx, eax
	shr ecx, 9
	and eax, 511

	; Save byte offset inside FAT sector: read_sectors_long clobbers RAX
	mov r13d, eax

	mov rdi, [fat32_fat_lba]
	add rdi, rcx
	mov rsi, 1
	mov rdx, fat32_scratch_buffer
	call read_sectors_long
	test rax, rax
	jz .error

	mov rbx, fat32_scratch_buffer
	mov ecx, [rbx + r13]
	and ecx, 0x0FFFFFFF

	cmp ecx, 0x0FFFFFF8
	jae .success

	mov [kernel_current_cluster], ecx
	jmp .load_file_loop
.success:
	mov rax, 1
	jmp .exit
.not_found:
.error:
	xor rax, rax
.exit:
	pop r15
	pop r14
	pop r13
	pop r12
	pop rbx
	pop rbp
	ret
cluster_to_lba:
	sub rdi, 2
	mov eax, [fat32_sec_per_cluster]
	mul rdi
	add rax, [fat32_data_lba]
	ret

%endif
