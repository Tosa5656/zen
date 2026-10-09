CC=x86_64-elf-gcc
LD=x86_64-elf-ld
AS=nasm

all: kernel

kernel:
	$(AS) -f elf64 kernel_entry.asm -o kernel_entry.o
	$(CC) -c kernel.c -o kernel.o -ffreestanding -O2 -Wall -Wextra -mno-red-zone -m64
	$(LD) -T linker.ld kernel_entry.o kernel.o -o kernel.bin
