# SPDX-License-Identifier: Unlicense
CC = gcc
LD = ld
READELF = readelf
QEMU = qemu-system-x86_64
XORRISO = xorriso
LIMINE_DIR ?= limine-binary
LIMINE ?= $(LIMINE_DIR)/limine
LIMINE_BIOS_SYS ?= $(LIMINE_DIR)/limine-bios.sys
LIMINE_BIOS_CD ?= $(LIMINE_DIR)/limine-bios-cd.bin
LIMINE_UEFI_CD ?= $(LIMINE_DIR)/limine-uefi-cd.bin
LIMINE_EFI ?= $(LIMINE_DIR)/BOOTX64.EFI
ISO ?= build/dawn-os.iso
KERNEL_ELF ?= build/dawn-kernel.elf

CFLAGS = -std=gnu11 -O2 -Wall -Wextra -Werror -Wconversion -Wshadow \
         -Wundef -ffreestanding -fno-stack-protector -fno-pic -fno-pie \
         -fno-asynchronous-unwind-tables -fno-unwind-tables \
         -m64 -mcmodel=kernel -mno-red-zone -mno-mmx -mno-sse -mno-sse2 \
         -msoft-float -Iinclude
ASFLAGS = -m64 -ffreestanding -fno-pic -fno-pie -mno-red-zone
LDFLAGS = -nostdlib -static -no-pie -z max-page-size=0x1000 --build-id=none

KERNEL_OBJECTS = build/boot.o build/main.o

.PHONY: all kernel clean check kernel-test iso iso-test run qemu-test

all: kernel

kernel: build/dawn-kernel.elf

build:
	mkdir -p build

build/boot.o: arch/x86_64/boot.S | build
	$(CC) $(ASFLAGS) -c $< -o $@

build/main.o: kernel/main.c include/limine_requests.h | build
	$(CC) $(CFLAGS) -c $< -o $@

build/dawn-kernel.elf: $(KERNEL_OBJECTS) linker.ld
	$(LD) $(LDFLAGS) -T linker.ld -o $@ $(KERNEL_OBJECTS)

kernel-test: build/dawn-kernel-test.elf

build/main-test.o: kernel/main.c include/limine_requests.h | build
	$(CC) $(CFLAGS) -DDAWN_KERNEL_TEST=1 -c $< -o $@

build/dawn-kernel-test.elf: build/boot.o build/main-test.o linker.ld
	$(LD) $(LDFLAGS) -T linker.ld -o $@ build/boot.o build/main-test.o

check: kernel
	@set -eu; \
	$(READELF) -h build/dawn-kernel.elf | grep -q 'Class:.*ELF64'; \
	$(READELF) -h build/dawn-kernel.elf | grep -q 'Machine:.*Advanced Micro Devices X86-64'; \
	$(READELF) -h build/dawn-kernel.elf | grep -q 'Type:.*EXEC'; \
	$(READELF) -S build/dawn-kernel.elf | grep -q '\.limine_requests'; \
	$(READELF) -l build/dawn-kernel.elf | grep -q 'R E'; \
	$(READELF) -l build/dawn-kernel.elf | grep -q 'RW '; \
	echo 'ELF check: PASS (ELF64 x86-64 executable; Limine requests and load segments present)'

iso: $(KERNEL_ELF)
	@command -v $(XORRISO) >/dev/null 2>&1 || { echo 'xorriso not found' >&2; exit 1; }
	@test -x "$(LIMINE)" || { echo 'Limine installer not found/executable: $(LIMINE)' >&2; exit 1; }
	@test -f "$(LIMINE_BIOS_SYS)" || { echo 'Missing $(LIMINE_BIOS_SYS)' >&2; exit 1; }
	@test -f "$(LIMINE_BIOS_CD)" || { echo 'Missing $(LIMINE_BIOS_CD)' >&2; exit 1; }
	@test -f "$(LIMINE_UEFI_CD)" || { echo 'Missing $(LIMINE_UEFI_CD)' >&2; exit 1; }
	@test -f "$(LIMINE_EFI)" || { echo 'Missing $(LIMINE_EFI)' >&2; exit 1; }
	rm -rf build/iso_root
	mkdir -p build/iso_root/boot/limine build/iso_root/EFI/BOOT
	cp "$(KERNEL_ELF)" build/iso_root/boot/dawn-kernel.elf
	cp limine.conf build/iso_root/boot/limine/limine.conf
	cp "$(LIMINE_BIOS_SYS)" "$(LIMINE_BIOS_CD)" "$(LIMINE_UEFI_CD)" build/iso_root/boot/limine/
	cp "$(LIMINE_EFI)" build/iso_root/EFI/BOOT/BOOTX64.EFI
	$(XORRISO) -as mkisofs -R -r -J \
		-b boot/limine/limine-bios-cd.bin -no-emul-boot -boot-load-size 4 -boot-info-table \
		--efi-boot boot/limine/limine-uefi-cd.bin -efi-boot-part --efi-boot-image \
		--protective-msdos-label build/iso_root -o "$(ISO)"
	$(LIMINE) bios-install "$(ISO)"

iso-test: kernel-test
	$(MAKE) iso KERNEL_ELF=build/dawn-kernel-test.elf ISO=build/dawn-os-test.iso

run: iso
	$(QEMU) -M q35 -m 256M -smp 1 -cdrom "$(ISO)" -serial stdio -no-reboot -no-shutdown

qemu-test: iso-test
	@command -v $(QEMU) >/dev/null 2>&1 || { echo 'QEMU not found' >&2; exit 1; }
	@command -v timeout >/dev/null 2>&1 || { echo 'timeout command not found' >&2; exit 1; }
	@set +e; timeout 15s $(QEMU) -M q35 -m 256M -smp 1 -cdrom build/dawn-os-test.iso \
		-serial stdio -display none -no-reboot -no-shutdown \
		-device isa-debug-exit,iobase=0xf4,iosize=0x04; status=$$?; \
	if [ $$status -eq 33 ]; then echo 'QEMU boot test: PASS'; exit 0; \
	elif [ $$status -eq 35 ]; then echo 'QEMU boot test: FAIL (kernel self-test failed)'; exit 1; \
	else echo "QEMU boot test: FAIL (exit status $$status)"; exit 1; fi

clean:
	rm -rf build
