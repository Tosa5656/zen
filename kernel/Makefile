CC=x86_64-elf-gcc
LD=x86_64-elf-ld
AS=nasm
ARCH=x86_64

CFLAGS=-ffreestanding -O2 -Wall -Wextra -mno-red-zone -m64 -fno-pie -fno-pic -I../arch/$(ARCH)/

ARCH_CODE=../arch/$(ARCH)/

all: kernel

kernel: idt
	$(AS) -f elf64 kernel_entry.asm -o kernel_entry.o
	$(CC) $(CFLAGS) -c kernel.c -o kernel.o
	$(CC) $(CFLAGS) -c memory.c -o memory.o
	$(CC) $(CFLAGS) -c serial.c -o serial.o
	$(CC) $(CFLAGS) -c pic.c -o pic.o
	$(LD) -T linker.ld kernel_entry.o kernel.o memory.o serial.o pic.o $(ARCH_CODE)/idt.o $(ARCH_CODE)/interrupts.o -o kernel.bin

idt:
	make -C ../arch/$(ARCH)/ idt

clean:
	rm -rf *.o
	rm -rf *.bin
