%ifndef FONT_ASM_INC
%define FONT_ASM_INC

; 8x16 bitmap font, 256 glyphs (16 bytes each). Embedded into the bootloader
; image so that the framebuffer console can render text right after the
; graphics mode has been set.
section .text
align 16
font_bitmap:
	incbin "base_font.bin"

%endif
