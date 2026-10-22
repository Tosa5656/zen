CC=x86_64-elf-gcc
LD=x86_64-elf-ld
AS=nasm
ARCH=x86_64

CFLAGS=-ffreestanding -O2 -Wall -Wextra -mno-red-zone -m64 -fno-pie -fno-pic -mcmodel=kernel -I../arch/$(ARCH)/ -I. -Iallocators

ARCH_CODE=../arch/$(ARCH)/

all: kernel

kernel: idt cpu syscall user spinlock
	$(AS) -f elf64 kernel_entry.asm -o kernel_entry.o
	$(CC) $(CFLAGS) -c kernel.c -o kernel.o
	$(CC) $(CFLAGS) -c memory.c -o memory.o
	$(CC) $(CFLAGS) -c serial.c -o serial.o
	$(CC) $(CFLAGS) -c pic.c -o pic.o
	$(CC) $(CFLAGS) -c timer.c -o timer.o
	$(CC) $(CFLAGS) -c acpi.c -o acpi.o
	$(CC) $(CFLAGS) -c apic.c -o apic.o
	$(CC) $(CFLAGS) -c gdt.c -o gdt.o
	$(AS) -f elf64 gdt_flush.asm -o gdt_flush.o
	$(CC) $(CFLAGS) -c panic.c -o panic.o
	$(CC) $(CFLAGS) -c allocators/buddy.c -o buddy.o
	$(CC) $(CFLAGS) -c allocators/slab.c -o slab.o
	$(CC) $(CFLAGS) -c heap.c -o heap.o
	$(CC) $(CFLAGS) -c keyboard.c -o keyboard.o
	$(CC) $(CFLAGS) -c scheduler.c -o scheduler.o
	$(CC) $(CFLAGS) -c syscall.c -o syscall.o
	$(CC) $(CFLAGS) -c usertest.c -o usertest.o
	$(CC) $(CFLAGS) -c ata.c -o ata.o
	$(CC) $(CFLAGS) -c block_device.c -o block_device.o
	$(CC) $(CFLAGS) -c vfs.c -o vfs.o
	$(CC) $(CFLAGS) -c fs_ram.c -o fs_ram.o
	$(CC) $(CFLAGS) -c fs_dev.c -o fs_dev.o
	$(CC) $(CFLAGS) -c fs/fat32.c -o fat32.o
	$(CC) $(CFLAGS) -c fd.c -o fd.o
	$(LD) -T linker.ld kernel_entry.o kernel.o memory.o serial.o pic.o timer.o acpi.o apic.o gdt.o gdt_flush.o panic.o buddy.o slab.o heap.o keyboard.o ata.o block_device.o vfs.o fs_ram.o fs_dev.o fat32.o fd.o $(ARCH_CODE)/idt.o $(ARCH_CODE)/interrupts.o $(ARCH_CODE)/cpu.o scheduler.o $(ARCH_CODE)/syscall.o syscall.o $(ARCH_CODE)/user.o $(ARCH_CODE)/spinlock.o usertest.o -o kernel.bin

idt:
	make -C ../arch/$(ARCH)/ idt

cpu:
	make -C ../arch/$(ARCH)/ cpu

syscall:
	make -C ../arch/$(ARCH)/ syscall

user:
	make -C ../arch/$(ARCH)/ user

spinlock:
	make -C ../arch/$(ARCH)/ spinlock

clean:
	rm -rf *.o
	rm -rf *.bin
	make -C ../arch/$(ARCH)/ clean
