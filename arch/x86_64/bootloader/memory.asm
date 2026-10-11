%ifndef MEMORY_ASM_INC
%define MEMORY_ASM_INC
bits 16

struc MemoryMapEntry
        .base_address: resq 1
        .length: resq 1
        .type: resd 1
        .extended_attributes: resd 1
endstruc

; input: DI - buffer offset
; output: AX - entries count
get_memory_map:
	push ebx
	push ecx
	push edx
	push bp
	push di

	xor ebx, ebx
	xor bp, bp
.loop:
	mov edx, 0x534D4150
	mov eax, 0xE820
	mov ecx, 24
	int 0x15
	jc .done

	cmp eax, 0x534D4150
	jne .error

	inc bp
	add di, 24

	test ebx, ebx
	jnz .loop
	jmp .done
.error:
	xor bp, bp
.done:
	mov ax, bp

	pop di
	pop bp
	pop edx
	pop ecx
	pop ebx
	ret
%endif
