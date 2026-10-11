all: bootloader

bootloader:
	cd bootloader && nasm -f bin bootloader_stage1.asm -o bootloader_stage1.bin
	cd bootloader && nasm -f bin bootloader_stage2.asm -o bootloader_stage2.bin
clean:
	rm -f bootloader/bootloader_stage1.bin bootloader/bootloader_stage2.bin

.PHONY: bootloader
