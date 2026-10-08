all: bootloader

bootloader:
	nasm -f bin bootloader/bootloader_stage1.asm -o bootloader/bootloader_stage1.bin
	nasm -f bin bootloader/bootloader_stage2.asm -o bootloader/bootloader_stage2.bin
clean:
	rm -rf bootloader/*.bin

.PHONY: bootloader
