# DAWN-OS Automated Test Suite Report

- **Source Commit:** `7bdbc071df879513ffd427876177c69a93b45a2f`
- **Timestamp (UTC):** `2026-11-23T05:44:36Z`
- **CI Run:** [GitHub Actions Run #45119599891](https://github.com/Pavithran26/DAWN-OS/actions/runs/45119599891)
- **Status:** **ALL PASSED**

---

## Boot Environment Results

| Environment | Bootloader | Firmware / Hypervisor | Exit Code | Result | Failures |
|---|---|---|---|---|---|
| **BIOS** | Limine v12.10.0 (BIOS CD) | QEMU q35 + SeaBIOS | 33 | **PASS** | 0 |
| **UEFI** | Limine v12.10.0 (BOOTX64.EFI) | QEMU q35 + OVMF (4M) | 33 | **PASS** | 0 |

---

## Verified Kernel Subsystems & Tests (64 Total)

| Subsystem | Verified Test Name | Status |
|---|---|---|
| Memory & Paging | memory-map validator edge cases | PASS |
| Memory & Paging | physical page allocator edge cases | PASS |
| Memory & Paging | virtual-memory map/translate/unmap tests | PASS |
| Memory & Paging | kernel-callable syscall dispatcher and user-memory helper unit tests (no ring-3 entry) | PASS |
| Memory & Paging | process spawning from in-memory ELF image and thread binding | PASS |
| Memory & Paging | dlibc strings, memory ops, heap allocator, package registry and app lifecycle | PASS |
| Memory & Paging | Limine revision, 4-level paging, memory map, HHDM, allocator, VM manager, IDT, ACPI MADT, xAPIC, IOAPIC, PIT IRQ, LAPIC timer, preemptive scheduler and framebuffer | PASS |
| Interrupts & APIC | breakpoint, PIC mask, IRQ-vector dispatch and PIT counter tests | PASS |
| Interrupts & APIC | ACPI MADT parser edge cases | PASS |
| Interrupts & APIC | real IOAPIC-routed PIT IRQ delivery | PASS |
| Interrupts & APIC | Local APIC one-shot timer interrupt | PASS |
| Interrupts & APIC | Local APIC timer calibration | PASS |
| Interrupts & APIC | SMP multi-core detection, BSP initialization and AP boot | PASS |
| Interrupts & APIC | kernel thread creation, interrupt-frame switching, round-robin yield, exit and stack guards | PASS |
| Interrupts & APIC | Local APIC periodic stop/restart checks | PASS |
| Interrupts & APIC | Local APIC timer invalid-argument checks | PASS |
| Interrupts & APIC | ACPI power states, reboot vectors and kernel panic diagnostics | PASS |
| Interrupts & APIC | Limine revision, 4-level paging, memory map, HHDM, allocator, VM manager, IDT, ACPI MADT, xAPIC, IOAPIC, PIT IRQ, LAPIC timer, preemptive scheduler and framebuffer | PASS |
| Process & Scheduler | process address-space and kernel-stack ownership, lifecycle, reaping and slot reuse | PASS |
| Process & Scheduler | user-mode entry validates active process mappings and stack alignment | PASS |
| Process & Scheduler | ELF64 header, load segments, permissions and bounds validation | PASS |
| Process & Scheduler | kernel thread creation, interrupt-frame switching, round-robin yield, exit and stack guards | PASS |
| Process & Scheduler | repeated timer-driven kernel-thread preemption | PASS |
| Process & Scheduler | process-bound kernel-thread CR3/TSS switching, yield, exit and reaping | PASS |
| Process & Scheduler | process-owned user IRETQ frame, code/stack permissions and trap-stack bounds | PASS |
| Process & Scheduler | controlled CPL3 ELF64 launch, syscall exit, scheduler return and process reaping | PASS |
| Process & Scheduler | parent child wait, blocking and collection | PASS |
| Process & Scheduler | process spawning from in-memory ELF image and thread binding | PASS |
| Process & Scheduler | task manager process list, performance graphs and controls | PASS |
| Process & Scheduler | init process spawning and user stack setup | PASS |
| Process & Scheduler | Limine revision, 4-level paging, memory map, HHDM, allocator, VM manager, IDT, ACPI MADT, xAPIC, IOAPIC, PIT IRQ, LAPIC timer, preemptive scheduler and framebuffer | PASS |
| Storage & Filesystem | parent child wait, blocking and collection | PASS |
| Storage & Filesystem | VFS mount, hierarchy, path resolution and operations | PASS |
| Storage & Filesystem | FAT32 filesystem parsing, read and write | PASS |
| Storage & Filesystem | ISO9660 primary volume descriptor and read | PASS |
| Storage & Filesystem | VFS dentry cache | PASS |
| Storage & Filesystem | block device registration and read/write | PASS |
| Storage & Filesystem | AHCI SATA controller and port detection | PASS |
| Storage & Filesystem | NVMe controller and namespace queues | PASS |
| Storage & Filesystem | MBR/GPT partition management and slice translation | PASS |
| Storage & Filesystem | installer disk verification, partition formatting, file copy and rescue shell | PASS |
| Networking & Drivers | PS/2 keyboard/mouse scancode decoding | PASS |
| Networking & Drivers | USB HID keyboard/mouse report parsing | PASS |
| Networking & Drivers | network checksum, loopback, UDP packet dispatch and socket lifecycle | PASS |
| Networking & Drivers | virtio-net PCI driver, ARP resolution and ICMP echo ping | PASS |
| Networking & Drivers | audio mixer, master volume, stream mixing and tone generation | PASS |
| GUI & Applications | desktop taskbar, launcher and composition | PASS |
| GUI & Applications | calculator application operations and button layout | PASS |
| GUI & Applications | calendar date calculations and month navigation | PASS |
| GUI & Applications | shell command parser, built-in commands and history | PASS |
| GUI & Applications | terminal emulator text grid, scrolling and render buffer | PASS |
| GUI & Applications | task manager process list, performance graphs and controls | PASS |
| GUI & Applications | settings serialization, defaults, clamping and window navigation | PASS |
| GUI & Applications | installer disk verification, partition formatting, file copy and rescue shell | PASS |
| GUI & Applications | security password hashing, user authentication, permissions and address boundaries | PASS |
| GUI & Applications | dlibc strings, memory ops, heap allocator, package registry and app lifecycle | PASS |
| GUI & Applications | service manager dependencies, daemon lifecycle, IPC queue and event pub/sub | PASS |

---
*Report generated automatically by DAWN-OS Release Automation.*
