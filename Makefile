ARCH=x86_64
CC=x86_64-elf-gcc
AS=nasm

all: bootloader
clean:
	rm -rf iso/
	rm -rf *.iso

	make -C arch/$(ARCH) clean

bootloader:
	make -C arch/$(ARCH) bootloader
