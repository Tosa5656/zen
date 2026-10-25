ARCH=x86_64
CC=x86_64-elf-gcc
AS=nasm

PARTED=parted
MFORMAT=mformat
MCOPY=mcopy

IMAGE=zen.img
DISK_SIZE=200 # Size in megabytes

ESP_START=2048
ESP_SECTORS=192512
ZEN_START=194560
ZEN_SECTORS=215007

ESP_OFFSET=$(shell echo $$(( $(ESP_START) * 512 )))
ZEN_OFFSET=$(shell echo $$(( $(ZEN_START) * 512 )))

all: bootloader kernel system

clean:
	rm -rf iso/
	rm -rf *.img
	rm -rf system

	make -C arch/$(ARCH) clean
	make -C kernel clean
	make -C os clean
	make -C zlibc clean
run:
	qemu-system-x86_64 -hda zen.img -m 2G -serial stdio

bootloader:
	make -C arch/$(ARCH) bootloader

kernel: zlibc bootloader
	make -C kernel

system: zlibc
	mkdir -p system/bin system/fonts

	make -C os
	cp -r fonts system/
	cp -r os/test_user.elf system/bin/

zlibc:
	make -C zlibc/

image: bootloader kernel system
	dd if=/dev/zero of=$(IMAGE) bs=1M count=$(DISK_SIZE)

	$(PARTED) -s $(IMAGE) mklabel gpt

	$(PARTED) -s $(IMAGE) mkpart ESP fat32 2048s 100MB
	$(PARTED) -s $(IMAGE) mkpart ZEN fat32 100MB 100%
	$(PARTED) -s $(IMAGE) set 1 boot on

	dd if=arch/$(ARCH)/bootloader/bootloader_stage1.bin of=$(IMAGE) bs=1 count=446 conv=notrunc
	dd if=arch/$(ARCH)/bootloader/bootloader_stage2.bin of=$(IMAGE) bs=512 seek=64 conv=notrunc

	$(MFORMAT) -F -i $(IMAGE)@@$(ESP_OFFSET) -T $(ESP_SECTORS) ::
	$(MCOPY) -i $(IMAGE)@@$(ESP_OFFSET) kernel/kernel.bin ::/KERNEL.BIN

	$(MFORMAT) -F -i $(IMAGE)@@$(ZEN_OFFSET) -T $(ZEN_SECTORS) ::
	$(MCOPY) -s -i $(IMAGE)@@$(ZEN_OFFSET) system/ ::/

.PHONY: all clean bootloader kernel system zlibc image iso
