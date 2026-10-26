CC=x86_64-elf-gcc
LD=x86_64-elf-ld
AS=nasm
CFLAGS=-ffreestanding -nostdlib -I../zlibc/include
ASFLAGS=-f elf64

LIBC=../zlibc/libc.a
CRT1=../zlibc/src/crt/crt1.o

all: test_user

clean:
	rm -rf *.o
	rm -rf *.bin
	rm -rf *.elf

test_user:
	$(CC) $(CFLAGS) $(CRT1) test_user.c -Wl,--start-group $(LIBC) -Wl,--end-group -o test_user.elf
