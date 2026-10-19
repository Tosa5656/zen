AS=nasm
CC=x86_64-elf-gcc
CFLAGS=-ffreestanding -O2 -Wall -Wextra -mno-red-zone -m64 -fno-pie -fno-pic -mcmodel=kernel -I../../kernel -I.

all: bootloader

bootloader:
	cd bootloader && $(AS) -f bin bootloader_stage1.asm -o bootloader_stage1.bin
	cd bootloader && $(AS) -f bin bootloader_stage2.asm -o bootloader_stage2.bin

idt:
	$(AS) -f elf64 interrupts.asm -o interrupts.o
	$(CC) $(CFLAGS) -c idt.c -o idt.o

cpu:
	$(CC) $(CFLAGS) -c cpu.c -o cpu.o

syscall:
	$(AS) -f elf64 syscall.asm -o syscall.o

user:
	$(AS) -f elf64 user.asm -o user.o

clean:
	rm -f bootloader/bootloader_stage1.bin bootloader/bootloader_stage2.bin
	rm -f *.o

.PHONY: bootloader
