bits 16

print_string:
	mov ah, 0x0E
	mov bh, 0
	mov bl, 0x07
.loop:
	lodsb
	cmp al, 0
	je .done

	int 0x10
	jmp .loop
.done:
	ret

bits 16