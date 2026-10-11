%ifndef BOOT_INFO_ASM_INC
%define BOOT_INFO_ASM_INC

struc BootInfo
	.magic: resd 1
	.size: resd 1
	.memory_map_address: resq 1
	.memory_map_entries: resq 1
	.framebuffer_address: resq 1
	.framebuffer_width: resd 1
	.framebuffer_height: resd 1
	.framebuffer_pitch: resd 1
	.framebuffer_bpp: resb 1
	.framebuffer_red_mask_shift: resb 1
	.framebuffer_green_mask_shift: resb 1
	.framebuffer_blue_mask_shift: resb 1
	._reserved1: resb 4
	.acpi_rsdp_address: resq 1
	.smbios_address: resq 1
endstruc

%endif
