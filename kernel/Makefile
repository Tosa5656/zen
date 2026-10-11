CC=x86_64-elf-gcc
LD=x86_64-elf-ld
AS=nasm

CFLAGS=-ffreestanding -O2 -Wall -Wextra -mno-red-zone -m64

all: kernel

kernel:
	$(AS) -f elf64 kernel_entry.asm -o kernel_entry.o
	$(CC) $(CFLAGS) -c kernel.c -o kernel.o
	$(CC) $(CFLAGS) -c memory.c -o memory.o
	$(CC) $(CFLAGS) -c serial.c -o serial.o
	$(LD) -T linker.ld kernel_entry.o kernel.o memory.o serial.o -o kernel.bin

clean:
	rm -rf *.o
	rm -rf *.bin
