%ifndef FRAMEBUFFER_ASM_INC
%define FRAMEBUFFER_ASM_INC

; ---------------------------------------------------------------------------
; Linear framebuffer text console.
;
; After init_graphics() has switched to a VBE mode, init_framebuffer() copies
; the mode parameters out of the VBE mode info block (vbe_buffer) into the
; state below, and the print_string_*_fb routines draw an 8x16 bitmap font
; (base_font.bin, 256 glyphs) straight into the linear framebuffer.
;
; Linear framebuffers normally live above 1 MB, so enable_unreal_mode() caches
; 4 GB segment limits; the real mode printer then reaches the framebuffer
; through FS.
; ---------------------------------------------------------------------------

bits 16

section .text

; Framebuffer console state. Kept in .text (which is writable here) so that it
; stays below 64 KB and can be addressed with 16-bit offsets from real mode.
fb_addr:        dd 0        ; physical address of the linear framebuffer
fb_pitch:       dd 0        ; bytes per scanline
fb_width:       dd 0        ; width in pixels
fb_height:      dd 0        ; height in pixels
fb_bpp:         db 0        ; bits per pixel
fb_bpp_bytes:   db 0        ; bytes per pixel
fb_red_shift:   db 0
fb_green_shift: db 0
fb_blue_shift:  db 0
fb_col:         db 0        ; scratch column counter
align 4
fb_fg:          dd 0        ; foreground colour (native pixel layout)
fb_bg:          dd 0        ; background colour
fb_cursor_x:    dd 0        ; cursor column, in character cells
fb_cursor_y:    dd 0        ; cursor row, in character cells

; ---------------------------------------------------------------------------
; init_framebuffer
; Reads the selected VBE mode info block from vbe_buffer and initialises the
; framebuffer console state. Must be called after a successful init_graphics.
; ---------------------------------------------------------------------------
init_framebuffer:
	mov eax, [vbe_buffer + 40]		; PhysBasePtr
	mov [fb_addr], eax

	movzx eax, word [vbe_buffer + 16]	; BytesPerScanLine
	mov [fb_pitch], eax

	movzx eax, word [vbe_buffer + 18]	; XResolution
	mov [fb_width], eax

	movzx eax, word [vbe_buffer + 20]	; YResolution
	mov [fb_height], eax

	mov al, [vbe_buffer + 25]		; BitsPerPixel
	mov [fb_bpp], al
	movzx eax, al
	shr eax, 3
	mov [fb_bpp_bytes], al

	mov al, [vbe_buffer + 32]		; RedFieldPosition
	mov [fb_red_shift], al
	mov al, [vbe_buffer + 34]		; GreenFieldPosition
	mov [fb_green_shift], al
	mov al, [vbe_buffer + 36]		; BlueFieldPosition
	mov [fb_blue_shift], al

	; foreground = white, expressed in the native pixel layout
	xor eax, eax
	mov edx, 0x000000FF
	mov cl, [fb_red_shift]
	shl edx, cl
	or eax, edx
	mov edx, 0x000000FF
	mov cl, [fb_green_shift]
	shl edx, cl
	or eax, edx
	mov edx, 0x000000FF
	mov cl, [fb_blue_shift]
	shl edx, cl
	or eax, edx
	mov [fb_fg], eax
	mov dword [fb_bg], 0

	mov dword [fb_cursor_x], 0
	mov dword [fb_cursor_y], 0
	ret

; ---------------------------------------------------------------------------
; enable_unreal_mode
; Loads the 4 GB flat data selector (0x10) into FS while briefly in protected
; mode and then drops back to real mode. The cached segment limit remains at
; 4 GB, which lets the real mode framebuffer printer reach memory above 1 MB
; through FS. DS/ES/GS are left as ordinary real mode segments so the BIOS
; interrupts issued afterwards still see normal (base 0) segments.
; Clobbers EAX.
; ---------------------------------------------------------------------------
enable_unreal_mode:
	cli
	lgdt [gdt_descriptor]

	mov eax, cr0
	or eax, 1
	mov cr0, eax
	jmp 0x08:.protected

bits 32
.protected:
	; Only FS needs the 4 GB flat selector: the framebuffer printer reaches the
	; linear framebuffer through FS. DS/ES/GS are intentionally left untouched so
	; that the BIOS interrupts called after the return still see normal segments.
	mov ax, 0x10
	mov fs, ax
	jmp 0x18:.pm16

bits 16
.pm16:
	mov eax, cr0
	and eax, 0xFFFFFFFE
	mov cr0, eax
	jmp 0x0000:.real

.real:
	ret

%endif
