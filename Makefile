ARCH=x86_64
CC=x86_64-elf-gcc
AS=nasm

PARTED=parted
MFORMAT=mformat
MCOPY=mcopy
XORRISO=xorriso

IMAGE=zen.img
ISO=zen.iso
DISK_SIZE=200 # Size in megabytes

all: bootloader kernel
clean:
	rm -rf iso/
	rm -rf *.iso *.img

	make -C arch/$(ARCH) clean
	make -C kernel clean

bootloader:
	make -C arch/$(ARCH) bootloader

kernel:
	make -C kernel

image: bootloader kernel
	dd if=/dev/zero of=$(IMAGE) bs=1M count=$(DISK_SIZE)

	$(PARTED) -s $(IMAGE) mklabel gpt

	$(PARTED) -s $(IMAGE) mkpart ESP fat32 2048s 100MB
	$(PARTED) -s $(IMAGE) mkpart ZEN 100MB 100%
	$(PARTED) -s $(IMAGE) set 1 boot on

	dd if=arch/$(ARCH)/bootloader/bootloader_stage1.bin of=$(IMAGE) bs=1 count=446 conv=notrunc
	dd if=arch/$(ARCH)/bootloader/bootloader_stage2.bin of=$(IMAGE) bs=512 seek=64 conv=notrunc

	$(MFORMAT) -F -i $(IMAGE)@@1048576 ::
	#$(MCOPY) -D r -i $(IMAGE)@@1048576 -s /dev/null ::/EFI
	#$(MCOPY) -D r -i $(IMAGE)@@1048576 -s /dev/null ::/EFI/BOOT

	#$(MCOPY) -i $(IMAGE)@@1048576 arch/$(ARCH)/bootloader/uefi/bootx64.efi ::/EFI/BOOT/BOOTX64.EFI
	$(MCOPY) -i $(IMAGE)@@1048576 kernel/kernel.bin ::/KERNEL.BIN

iso: image
	$(XORRISO) -as mkisofs -b $(IMAGE) -hard-disk-boot -no-emul-boot -boot-load-size 4 -o $(ISO) $(IMAGE)

.PHONY: all clean bootloader kernel image iso
