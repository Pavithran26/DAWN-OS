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
OVMF_CODE ?= /usr/share/OVMF/OVMF_CODE.fd
OVMF_VARS ?= /usr/share/OVMF/OVMF_VARS.fd
ISO ?= build/dawn-os.iso
KERNEL_ELF ?= build/dawn-kernel.elf

CFLAGS = -std=gnu11 -O2 -Wall -Wextra -Werror -Wconversion -Wshadow \
         -Wundef -ffreestanding -fno-stack-protector -fno-pic -fno-pie \
         -fno-asynchronous-unwind-tables -fno-unwind-tables \
         -m64 -mcmodel=kernel -mno-red-zone -mno-mmx -mno-sse -mno-sse2 \
         -msoft-float -Iinclude
ASFLAGS = -m64 -ffreestanding -fno-pic -fno-pie -mno-red-zone
LDFLAGS = -nostdlib -static -no-pie -z max-page-size=0x1000 --build-id=none

KERNEL_OBJECTS = build/boot.o build/gdt.o build/main.o build/physical_memory.o build/page_table.o build/interrupts.o build/interrupt-stubs.o build/acpi.o build/apic.o build/scheduler.o build/syscall.o build/user-mode.o build/process.o build/elf-loader.o build/vfs.o build/fat32.o build/iso9660.o build/vfs-cache.o build/file-explorer.o build/block_dev.o build/pci.o build/ahci.o build/nvme.o build/partition.o build/storage_manager.o build/device.o build/ps2.o build/xhci.o build/usb_hid.o build/input.o build/gfx.o build/window_manager.o build/desktop.o build/shell.o build/terminal.o build/task_manager.o build/net.o build/audio.o build/settings.o build/installer.o build/security.o

.PHONY: all kernel clean check kernel-test iso iso-test run qemu-test qemu-uefi-test qemu-test-all

all: kernel

kernel: build/dawn-kernel.elf

build:
	mkdir -p build

build/boot.o: arch/x86_64/boot.S | build
	$(CC) $(ASFLAGS) -c $< -o $@

build/gdt.o: kernel/gdt.c include/gdt.h arch/x86_64/gdt.S | build
	$(CC) $(CFLAGS) -c kernel/gdt.c -o build/gdt-c.o
	$(CC) $(ASFLAGS) -c arch/x86_64/gdt.S -o build/gdt-asm.o
	$(LD) -r -o $@ build/gdt-c.o build/gdt-asm.o

build/main.o: kernel/main.c include/gdt.h include/process.h include/limine_requests.h include/physical_memory.h include/page_table.h include/interrupts.h include/acpi.h include/apic.h include/scheduler.h include/syscall.h include/elf_loader.h include/vfs.h include/fat32.h include/iso9660.h include/vfs_cache.h include/file_explorer.h include/block_dev.h include/pci.h include/ahci.h include/nvme.h include/partition.h include/storage_manager.h include/device.h include/ps2.h include/xhci.h include/usb_hid.h include/input.h include/gfx.h include/window_manager.h include/desktop.h include/shell.h include/terminal.h include/task_manager.h include/net.h include/audio.h include/settings.h include/installer.h include/security.h | build
	$(CC) $(CFLAGS) -c $< -o $@

build/physical_memory.o: kernel/physical_memory.c include/physical_memory.h include/limine_requests.h | build
	$(CC) $(CFLAGS) -c $< -o $@

build/acpi.o: kernel/acpi.c include/acpi.h include/limine_requests.h | build
	$(CC) $(CFLAGS) -c $< -o $@

build/apic.o: kernel/apic.c include/apic.h include/acpi.h include/page_table.h | build
	$(CC) $(CFLAGS) -c $< -o $@

build/scheduler.o: kernel/scheduler.c include/scheduler.h include/interrupts.h include/process.h include/gdt.h include/page_table.h include/physical_memory.h include/elf_loader.h include/syscall.h include/apic.h | build
	$(CC) $(CFLAGS) -c $< -o $@

build/syscall.o: kernel/syscall.c include/syscall.h include/page_table.h include/process.h | build
	$(CC) $(CFLAGS) -c $< -o $@

build/process.o: kernel/process.c include/process.h include/page_table.h include/gdt.h include/physical_memory.h include/elf_loader.h include/scheduler.h | build
	$(CC) $(CFLAGS) -c $< -o $@
build/user-mode.o: kernel/user_mode.c include/user_mode.h include/gdt.h include/page_table.h include/process.h include/physical_memory.h arch/x86_64/user_mode.S | build
	$(CC) $(CFLAGS) -c kernel/user_mode.c -o build/user-mode-c.o
	$(CC) $(ASFLAGS) -c arch/x86_64/user_mode.S -o build/user-mode-asm.o
	$(LD) -r -o $@ build/user-mode-c.o build/user-mode-asm.o

build/elf-loader.o: kernel/elf_loader.c include/elf_loader.h include/page_table.h include/physical_memory.h include/process.h | build
	$(CC) $(CFLAGS) -c $< -o $@

build/vfs.o: kernel/vfs.c include/vfs.h | build
	$(CC) $(CFLAGS) -c $< -o $@

build/fat32.o: kernel/fat32.c include/fat32.h include/vfs.h | build
	$(CC) $(CFLAGS) -c $< -o $@

build/iso9660.o: kernel/iso9660.c include/iso9660.h include/vfs.h | build
	$(CC) $(CFLAGS) -c $< -o $@

build/vfs-cache.o: kernel/vfs_cache.c include/vfs_cache.h include/vfs.h | build
	$(CC) $(CFLAGS) -c $< -o $@

build/file-explorer.o: kernel/file_explorer.c include/file_explorer.h include/vfs.h | build
	$(CC) $(CFLAGS) -c $< -o $@

build/block_dev.o: kernel/block_dev.c include/block_dev.h | build
	$(CC) $(CFLAGS) -c $< -o $@

build/pci.o: kernel/pci.c include/pci.h | build
	$(CC) $(CFLAGS) -c $< -o $@

build/ahci.o: kernel/ahci.c include/ahci.h include/block_dev.h include/pci.h | build
	$(CC) $(CFLAGS) -c $< -o $@

build/nvme.o: kernel/nvme.c include/nvme.h include/block_dev.h include/pci.h | build
	$(CC) $(CFLAGS) -c $< -o $@

build/partition.o: kernel/partition.c include/partition.h include/block_dev.h | build
	$(CC) $(CFLAGS) -c $< -o $@

build/storage_manager.o: kernel/storage_manager.c include/storage_manager.h include/block_dev.h include/pci.h include/ahci.h include/nvme.h include/partition.h include/vfs.h include/fat32.h include/iso9660.h | build
	$(CC) $(CFLAGS) -c $< -o $@

build/device.o: kernel/device.c include/device.h | build
	$(CC) $(CFLAGS) -c $< -o $@

build/ps2.o: kernel/ps2.c include/ps2.h include/device.h | build
	$(CC) $(CFLAGS) -c $< -o $@

build/xhci.o: kernel/xhci.c include/xhci.h include/device.h include/pci.h | build
	$(CC) $(CFLAGS) -c $< -o $@

build/usb_hid.o: kernel/usb_hid.c include/usb_hid.h include/ps2.h include/device.h | build
	$(CC) $(CFLAGS) -c $< -o $@

build/input.o: kernel/input.c include/input.h include/ps2.h include/usb_hid.h | build
	$(CC) $(CFLAGS) -c $< -o $@

build/gfx.o: kernel/gfx.c include/gfx.h include/limine_requests.h | build
	$(CC) $(CFLAGS) -c $< -o $@

build/window_manager.o: kernel/window_manager.c include/window_manager.h include/gfx.h include/input.h | build
	$(CC) $(CFLAGS) -c $< -o $@

build/desktop.o: kernel/desktop.c include/desktop.h include/window_manager.h include/gfx.h include/input.h | build
	$(CC) $(CFLAGS) -c $< -o $@

build/shell.o: kernel/shell.c include/shell.h include/vfs.h include/process.h include/storage_manager.h include/physical_memory.h include/file_explorer.h | build
	$(CC) $(CFLAGS) -c $< -o $@

build/terminal.o: kernel/terminal.c include/terminal.h include/shell.h include/gfx.h include/window_manager.h include/input.h | build
	$(CC) $(CFLAGS) -c $< -o $@

build/task_manager.o: kernel/task_manager.c include/task_manager.h include/gfx.h include/window_manager.h include/process.h include/physical_memory.h | build
	$(CC) $(CFLAGS) -c $< -o $@

build/net.o: kernel/net.c include/net.h include/device.h | build
	$(CC) $(CFLAGS) -c $< -o $@

build/audio.o: kernel/audio.c include/audio.h include/device.h include/pci.h | build
	$(CC) $(CFLAGS) -c $< -o $@

build/settings.o: kernel/settings.c include/settings.h include/vfs.h include/audio.h include/net.h include/window_manager.h include/gfx.h | build
	$(CC) $(CFLAGS) -c $< -o $@

build/installer.o: kernel/installer.c include/installer.h include/storage_manager.h include/partition.h include/fat32.h include/vfs.h | build
	$(CC) $(CFLAGS) -c $< -o $@

build/security.o: kernel/security.c include/security.h | build
	$(CC) $(CFLAGS) -c $< -o $@

build/page_table.o: kernel/page_table.c include/page_table.h include/physical_memory.h include/limine_requests.h | build
	$(CC) $(CFLAGS) -c $< -o $@

build/interrupts.o: kernel/interrupts.c include/interrupts.h include/gdt.h include/apic.h include/scheduler.h include/process.h include/page_table.h include/physical_memory.h | build
	$(CC) $(CFLAGS) -c $< -o $@

build/interrupt-stubs.o: arch/x86_64/interrupts.S | build
	$(CC) $(ASFLAGS) -c $< -o $@

build/dawn-kernel.elf: $(KERNEL_OBJECTS) linker.ld
	$(LD) $(LDFLAGS) -T linker.ld -o $@ $(KERNEL_OBJECTS)

kernel-test: build/dawn-kernel-test.elf

build/main-test.o: kernel/main.c include/gdt.h include/process.h include/limine_requests.h include/physical_memory.h include/page_table.h include/interrupts.h include/acpi.h include/apic.h include/scheduler.h include/syscall.h include/user_mode.h include/elf_loader.h include/vfs.h include/fat32.h include/iso9660.h include/vfs_cache.h include/file_explorer.h include/block_dev.h include/pci.h include/ahci.h include/nvme.h include/partition.h include/storage_manager.h include/device.h include/ps2.h include/xhci.h include/usb_hid.h include/input.h include/gfx.h include/window_manager.h include/desktop.h include/shell.h include/terminal.h include/task_manager.h include/net.h include/audio.h include/settings.h include/installer.h include/security.h | build
	$(CC) $(CFLAGS) -DDAWN_KERNEL_TEST=1 -c $< -o $@

build/physical-memory-test.o: kernel/physical_memory.c include/physical_memory.h include/limine_requests.h | build
	$(CC) $(CFLAGS) -DDAWN_KERNEL_TEST=1 -c $< -o $@

build/page-table-test.o: kernel/page_table.c include/page_table.h include/physical_memory.h include/limine_requests.h | build
	$(CC) $(CFLAGS) -DDAWN_KERNEL_TEST=1 -c $< -o $@

build/interrupts-test.o: kernel/interrupts.c include/interrupts.h include/gdt.h include/apic.h include/scheduler.h include/process.h include/page_table.h include/physical_memory.h | build
	$(CC) $(CFLAGS) -DDAWN_KERNEL_TEST=1 -c $< -o $@

build/acpi-test.o: kernel/acpi.c include/acpi.h include/limine_requests.h | build
	$(CC) $(CFLAGS) -DDAWN_KERNEL_TEST=1 -c $< -o $@

build/apic-test.o: kernel/apic.c include/apic.h include/acpi.h include/page_table.h | build
	$(CC) $(CFLAGS) -DDAWN_KERNEL_TEST=1 -c $< -o $@

build/scheduler-test.o: kernel/scheduler.c include/scheduler.h include/interrupts.h include/process.h include/gdt.h include/page_table.h include/physical_memory.h include/elf_loader.h include/syscall.h include/apic.h | build
	$(CC) $(CFLAGS) -DDAWN_KERNEL_TEST=1 -c $< -o $@

build/syscall-test.o: kernel/syscall.c include/syscall.h include/page_table.h include/process.h | build
	$(CC) $(CFLAGS) -DDAWN_KERNEL_TEST=1 -c $< -o $@

build/process-test.o: kernel/process.c include/process.h include/page_table.h include/gdt.h include/physical_memory.h include/elf_loader.h include/scheduler.h | build
	$(CC) $(CFLAGS) -DDAWN_KERNEL_TEST=1 -c $< -o $@
build/user-mode-test.o: kernel/user_mode.c include/user_mode.h include/gdt.h include/page_table.h include/process.h include/physical_memory.h | build
	$(CC) $(CFLAGS) -DDAWN_KERNEL_TEST=1 -c $< -o $@

build/elf-loader-test.o: kernel/elf_loader.c include/elf_loader.h include/page_table.h include/physical_memory.h include/process.h | build
	$(CC) $(CFLAGS) -DDAWN_KERNEL_TEST=1 -c $< -o $@

build/vfs-test.o: kernel/vfs.c include/vfs.h | build
	$(CC) $(CFLAGS) -DDAWN_KERNEL_TEST=1 -c $< -o $@

build/fat32-test.o: kernel/fat32.c include/fat32.h include/vfs.h | build
	$(CC) $(CFLAGS) -DDAWN_KERNEL_TEST=1 -c $< -o $@

build/iso9660-test.o: kernel/iso9660.c include/iso9660.h include/vfs.h | build
	$(CC) $(CFLAGS) -DDAWN_KERNEL_TEST=1 -c $< -o $@

build/vfs-cache-test.o: kernel/vfs_cache.c include/vfs_cache.h include/vfs.h | build
	$(CC) $(CFLAGS) -DDAWN_KERNEL_TEST=1 -c $< -o $@

build/file-explorer-test.o: kernel/file_explorer.c include/file_explorer.h include/vfs.h | build
	$(CC) $(CFLAGS) -DDAWN_KERNEL_TEST=1 -c $< -o $@

build/block-dev-test.o: kernel/block_dev.c include/block_dev.h | build
	$(CC) $(CFLAGS) -DDAWN_KERNEL_TEST=1 -c $< -o $@

build/pci-test.o: kernel/pci.c include/pci.h | build
	$(CC) $(CFLAGS) -DDAWN_KERNEL_TEST=1 -c $< -o $@

build/ahci-test.o: kernel/ahci.c include/ahci.h include/block_dev.h include/pci.h | build
	$(CC) $(CFLAGS) -DDAWN_KERNEL_TEST=1 -c $< -o $@

build/nvme-test.o: kernel/nvme.c include/nvme.h include/block_dev.h include/pci.h | build
	$(CC) $(CFLAGS) -DDAWN_KERNEL_TEST=1 -c $< -o $@

build/partition-test.o: kernel/partition.c include/partition.h include/block_dev.h | build
	$(CC) $(CFLAGS) -DDAWN_KERNEL_TEST=1 -c $< -o $@

build/storage-manager-test.o: kernel/storage_manager.c include/storage_manager.h include/block_dev.h include/pci.h include/ahci.h include/nvme.h include/partition.h include/vfs.h include/fat32.h include/iso9660.h | build
	$(CC) $(CFLAGS) -DDAWN_KERNEL_TEST=1 -c $< -o $@

build/device-test.o: kernel/device.c include/device.h | build
	$(CC) $(CFLAGS) -DDAWN_KERNEL_TEST=1 -c $< -o $@

build/ps2-test.o: kernel/ps2.c include/ps2.h include/device.h | build
	$(CC) $(CFLAGS) -DDAWN_KERNEL_TEST=1 -c $< -o $@

build/xhci-test.o: kernel/xhci.c include/xhci.h include/device.h include/pci.h | build
	$(CC) $(CFLAGS) -DDAWN_KERNEL_TEST=1 -c $< -o $@

build/usb-hid-test.o: kernel/usb_hid.c include/usb_hid.h include/ps2.h include/device.h | build
	$(CC) $(CFLAGS) -DDAWN_KERNEL_TEST=1 -c $< -o $@

build/input-test.o: kernel/input.c include/input.h include/ps2.h include/usb_hid.h | build
	$(CC) $(CFLAGS) -DDAWN_KERNEL_TEST=1 -c $< -o $@

build/gfx-test.o: kernel/gfx.c include/gfx.h include/limine_requests.h | build
	$(CC) $(CFLAGS) -DDAWN_KERNEL_TEST=1 -c $< -o $@

build/window-manager-test.o: kernel/window_manager.c include/window_manager.h include/gfx.h include/input.h | build
	$(CC) $(CFLAGS) -DDAWN_KERNEL_TEST=1 -c $< -o $@

build/desktop-test.o: kernel/desktop.c include/desktop.h include/window_manager.h include/gfx.h include/input.h | build
	$(CC) $(CFLAGS) -DDAWN_KERNEL_TEST=1 -c $< -o $@

build/shell-test.o: kernel/shell.c include/shell.h include/vfs.h include/process.h include/storage_manager.h include/physical_memory.h include/file_explorer.h | build
	$(CC) $(CFLAGS) -DDAWN_KERNEL_TEST=1 -c $< -o $@

build/terminal-test.o: kernel/terminal.c include/terminal.h include/shell.h include/gfx.h include/window_manager.h include/input.h | build
	$(CC) $(CFLAGS) -DDAWN_KERNEL_TEST=1 -c $< -o $@

build/task-manager-test.o: kernel/task_manager.c include/task_manager.h include/gfx.h include/window_manager.h include/process.h include/physical_memory.h | build
	$(CC) $(CFLAGS) -DDAWN_KERNEL_TEST=1 -c $< -o $@

build/net-test.o: kernel/net.c include/net.h include/device.h | build
	$(CC) $(CFLAGS) -DDAWN_KERNEL_TEST=1 -c $< -o $@

build/audio-test.o: kernel/audio.c include/audio.h include/device.h include/pci.h | build
	$(CC) $(CFLAGS) -DDAWN_KERNEL_TEST=1 -c $< -o $@

build/settings-test.o: kernel/settings.c include/settings.h include/vfs.h include/audio.h include/net.h include/window_manager.h include/gfx.h | build
	$(CC) $(CFLAGS) -DDAWN_KERNEL_TEST=1 -c $< -o $@

build/installer-test.o: kernel/installer.c include/installer.h include/storage_manager.h include/partition.h include/fat32.h include/vfs.h | build
	$(CC) $(CFLAGS) -DDAWN_KERNEL_TEST=1 -c $< -o $@

build/security-test.o: kernel/security.c include/security.h | build
	$(CC) $(CFLAGS) -DDAWN_KERNEL_TEST=1 -c $< -o $@

build/dawn-kernel-test.elf: build/boot.o build/gdt.o build/main-test.o build/physical-memory-test.o build/page-table-test.o build/interrupts-test.o build/interrupt-stubs.o build/acpi-test.o build/apic-test.o build/scheduler-test.o build/syscall-test.o build/user-mode-test.o build/user-mode.o build/process-test.o build/elf-loader-test.o build/vfs-test.o build/fat32-test.o build/iso9660-test.o build/vfs-cache-test.o build/file-explorer-test.o build/block-dev-test.o build/pci-test.o build/ahci-test.o build/nvme-test.o build/partition-test.o build/storage-manager-test.o build/device-test.o build/ps2-test.o build/xhci-test.o build/usb-hid-test.o build/input-test.o build/gfx-test.o build/window-manager-test.o build/desktop-test.o build/shell-test.o build/terminal-test.o build/task-manager-test.o build/net-test.o build/audio-test.o build/settings-test.o build/installer-test.o build/security-test.o linker.ld
	$(LD) $(LDFLAGS) -T linker.ld -o $@ build/boot.o build/gdt.o build/main-test.o build/physical-memory-test.o build/page-table-test.o build/interrupts-test.o build/interrupt-stubs.o build/acpi-test.o build/apic-test.o build/scheduler-test.o build/syscall-test.o build/user-mode-test.o build/user-mode.o build/process-test.o build/elf-loader-test.o build/vfs-test.o build/fat32-test.o build/iso9660-test.o build/vfs-cache-test.o build/file-explorer-test.o build/block-dev-test.o build/pci-test.o build/ahci-test.o build/nvme-test.o build/partition-test.o build/storage-manager-test.o build/device-test.o build/ps2-test.o build/xhci-test.o build/usb-hid-test.o build/input-test.o build/gfx-test.o build/window-manager-test.o build/desktop-test.o build/shell-test.o build/terminal-test.o build/task-manager-test.o build/net-test.o build/audio-test.o build/settings-test.o build/installer-test.o build/security-test.o

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
	cp "$(KERNEL_ELF)" build/iso_root/boot/dawnos.elf
	cp limine.conf build/iso_root/boot/limine/limine.conf
	cp "$(LIMINE_BIOS_SYS)" "$(LIMINE_BIOS_CD)" "$(LIMINE_UEFI_CD)" build/iso_root/boot/limine/
	cp "$(LIMINE_EFI)" build/iso_root/EFI/BOOT/BOOTX64.EFI
	$(XORRISO) -as mkisofs -R -r -J -hfsplus -apm-block-size 2048 \
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
	@set +e; timeout 20s $(QEMU) -M q35 -m 256M -smp 1 -cdrom build/dawn-os-test.iso -boot order=d,menu=off -vga std \
		-serial file:build/qemu-bios-serial.log -display none -no-reboot -no-shutdown \
		-d guest_errors,cpu_reset -D build/qemu-bios-debug.log \
		-device isa-debug-exit,iobase=0xf4,iosize=0x04; status=$$?; \
	cat build/qemu-bios-serial.log; \
	if [ $$status -eq 33 ]; then echo 'QEMU BIOS boot test: PASS'; exit 0; \
	elif [ $$status -eq 35 ]; then echo 'QEMU BIOS boot test: FAIL (kernel self-test failed)'; exit 1; \
	else echo "QEMU BIOS boot test: FAIL (exit status $$status)"; exit 1; fi

qemu-uefi-test: iso-test
	@command -v $(QEMU) >/dev/null 2>&1 || { echo 'QEMU not found' >&2; exit 1; }
	@test -f "$(OVMF_CODE)" || { echo 'OVMF UEFI code image not found: $(OVMF_CODE)' >&2; exit 1; }
	@test -f "$(OVMF_VARS)" || { echo 'OVMF UEFI variables image not found: $(OVMF_VARS)' >&2; exit 1; }
	@command -v timeout >/dev/null 2>&1 || { echo 'timeout command not found' >&2; exit 1; }
	cp "$(OVMF_VARS)" build/OVMF_VARS.fd
	@set +e; timeout 25s $(QEMU) -M q35 -m 256M -smp 1 \
		-drive if=pflash,format=raw,unit=0,file="$(OVMF_CODE)",readonly=on \
		-drive if=pflash,format=raw,unit=1,file=build/OVMF_VARS.fd \
		-cdrom build/dawn-os-test.iso -boot order=d,menu=off -vga std \
		-serial file:build/qemu-uefi-serial.log -display none -no-reboot -no-shutdown \
		-d guest_errors,cpu_reset -D build/qemu-uefi-debug.log \
		-device isa-debug-exit,iobase=0xf4,iosize=0x04; status=$$?; \
	cat build/qemu-uefi-serial.log; \
	if [ $$status -eq 33 ]; then echo 'QEMU UEFI boot test: PASS'; exit 0; \
	elif [ $$status -eq 35 ]; then echo 'QEMU UEFI boot test: FAIL (kernel self-test failed)'; exit 1; \
	else echo "QEMU UEFI boot test: FAIL (exit status $$status)"; exit 1; fi

qemu-test-all: qemu-test qemu-uefi-test

clean:
	rm -rf build
