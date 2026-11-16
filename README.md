# DAWN-OS

DAWN-OS is an experimental, freestanding x86_64 operating-system project with a server-first direction. Its long-term goals are reliable boot and recovery, low request latency, high sustained concurrency, and efficient CPU and memory use. These are goals, not measured results.

## Current status

The repository contains the first Phase A kernel scaffold:

- A 64-bit Limine entry path with a dedicated bootstrap stack.
- Limine request delimiters, a base-revision 6 tag, and a framebuffer request.
- Bounded COM1 serial diagnostics; serial output failure cannot trap the kernel in an infinite polling loop.
- A deterministic kernel self-test mode that reports PASS/FAIL over serial and uses QEMU's `isa-debug-exit` device when available.
- An ELF layout with separate executable, read-only, and writable load segments.
- A GitHub Actions boot-validation workflow for ISO creation and QEMU BIOS/UEFI attempts.
- A Limine boot-memory-map request, bounded structural validator, malformed-map self-tests, and serial summary of usable/reclaimable regions.
- A Limine HHDM request with runtime-selected offset validation; no fixed direct-map address is assumed.
- A bitmap physical page-frame allocator with usable-frame ownership tracking and invalid/double-free rejection; initial tracking is limited to physical addresses below 4 GiB and no bootloader/ACPI memory is reclaimed.
- An x86_64 four-level page-table manager attached to Limine's active CR3, with checked 4 KiB mappings, software translation, unmapping, NX/write/user permission checks, and QEMU self-tests.
- A 256-entry x86_64 IDT with CPU exception stubs, fatal exception diagnostics, ACPI RSDP/XSDT/RSDT/MADT discovery, a four-level-page-table uncached MMIO mapping for APIC registers, xAPIC + IOAPIC setup, and an IOAPIC-routed PIT IRQ0 test. The legacy PIC remains masked. See [ACPI/APIC implementation notes](docs/ACPI_APIC.md).
- An optional graphical administration shell is planned; terminal, file explorer, partition manager, task manager, and the green liquid-neomorphism interface are **not implemented yet**. See [the graphical shell design](docs/GUI_SHELL_PLAN.md) and [Phase D milestones](docs/PHASE_D_PLAN.md).

This is **not a complete operating system**. The current interrupt path is single-CPU xAPIC/IOAPIC with one ACPI-routed PIT source tested in QEMU; SMP interrupt routing, scheduler, separate process address spaces, filesystem, userspace, syscall ABI, service supervisor, storage/network drivers, network stack, and server applications are not implemented. The physical allocator returns physical frame addresses only and does not zero frames or provide a heap. The early page-table manager can map allocator-owned frames in the active root, but it does not create isolated process address spaces or support concurrent CPUs. Bootloader-reclaimable and ACPI memory remain reserved. An ELF build or ISO creation is not proof of a successful boot; check the CI run logs and artifacts for boot evidence.

## Build the kernel ELF

Requirements: GNU Make, GCC with x86_64 code-generation support, and GNU binutils (`ld` and `readelf`). From the repository root:

```sh
make clean kernel
make check
file build/dawn-kernel.elf
make kernel-test
```

The build is freestanding: it disables hosted runtime assumptions, PIE/PIC, stack protector, the x86_64 red zone, and SIMD code generation. A Linux-hosted compiler is only a build tool, not a DAWN-OS runtime.

## Build a BIOS/UEFI hybrid ISO

Obtain the Limine v12.10.0 binary package and place its `limine` installer, `limine-bios.sys`, `limine-bios-cd.bin`, `limine-uefi-cd.bin`, and `BOOTX64.EFI` under `limine-binary/` (or set `LIMINE_DIR` to that directory). Install `xorriso`, then run:

```sh
make iso LIMINE_DIR=limine-binary
```

The ISO target creates a hybrid BIOS/UEFI image and installs Limine's BIOS stage. The build does not download or silently replace third-party bootloader binaries.

## Run DAWN-OS in a virtual machine

QEMU is the primary development VM. Install `qemu-system-x86`, `ovmf` (UEFI firmware), and `timeout` in addition to the ISO build requirements.

Run the interactive BIOS VM:

```sh
make run LIMINE_DIR=limine-binary
```

Run automated self-tests under BIOS and UEFI:

```sh
make qemu-test LIMINE_DIR=limine-binary
make qemu-uefi-test LIMINE_DIR=limine-binary OVMF_CODE=/usr/share/OVMF/OVMF_CODE.fd OVMF_VARS=/usr/share/OVMF/OVMF_VARS.fd
```

Or run both with `make qemu-test-all`. The QEMU test uses the `isa-debug-exit` device: exit status 33 means the kernel self-test reported success, 35 means it reported failure. Serial logs are saved under `build/`. UEFI firmware paths vary by distribution; set `OVMF_CODE` and `OVMF_VARS` to a matching OVMF code/variables pair. The test copies the variables image into `build/` before boot so the installed firmware template is not modified.

The resulting ISO can also be attached to VirtualBox or VMware as a virtual optical disk after QEMU validation. Those hypervisors are not yet covered by automated CI.

## Continuous integration

The workflow `.github/workflows/phase-a-boot.yml` runs on pushes to `main` and `bootstrap/**`, pull requests targeting `main`, and manual dispatch. It installs the build/VM tools, downloads the pinned Limine v12.10.0 binary package, builds and checks both kernels, creates a test ISO, and attempts BIOS and UEFI boots in QEMU. It uploads the ISO and serial logs as workflow artifacts.

A green workflow is evidence only for the tests that actually ran in that environment. Review the logs; do not infer physical-hardware compatibility from QEMU success.

## Phase A verification

The latest recorded GitHub Actions run passed kernel compilation, ELF checks, hybrid BIOS/UEFI ISO creation, and QEMU boots under both SeaBIOS and OVMF UEFI. This is VM evidence only; physical BIOS/UEFI systems and Secure Boot are not yet validated.

## Phase B — memory and CPU foundations

The first increment requests, validates, and reports the Limine memory map. See [the memory-map implementation notes](docs/MEMORY_MAP.md). It deliberately does not enable allocation yet.

Remaining Phase B work:
- Harden allocation accounting and add synchronization after interrupt/CPU foundations exist.
- Extend page-table permission/lifetime tests; add address-space ownership and mapping teardown after process and fault-handling foundations.
- Expand APIC/IOAPIC routing and ACPI coverage; the first MADT-discovered ISA PIT IRQ route is implemented and QEMU-tested.
- Add kernel threads, context switching, and a scheduler after the interrupt/timer foundations are tested.
- Add SMP only after CPU startup, per-CPU state, synchronization, and locking are defined.

## Roadmap

1. **Boot and test:** verify Limine boot on QEMU, preserve serial diagnostics, and expand the kernel test harness.
2. **Memory and interrupts:** validate the boot memory map, initialize the physical page allocator and extend virtual-memory support, then extend APIC/IOAPIC routing coverage and add fault recovery.
3. **Execution and SMP:** implement kernel threads, context switching, a scheduler, CPU startup, synchronization, and per-CPU state.
4. **Userspace and storage:** define a syscall ABI and privilege boundary, add a compact runtime and service supervisor, then add filesystem and virtio block I/O.
5. **Networking:** add virtio networking and a documented network stack, then implement a small reproducible HTTP workload.
6. **Measurement and optimization:** establish correctness baselines and measure throughput, p50/p95/p99 latency, CPU time per request, RAM, service-ready time, and failure recovery. Compare with Linux only under equivalent hardware/VM, resource limits, devices, service semantics, workload, configuration, and duration.

No performance or security superiority over Linux is claimed. A small kernel ELF is not directly comparable to a complete Linux distribution.

## Licensing

The project retains the repository's Unlicense dedication. The minimal Limine protocol declarations in `include/limine_requests.h` are derived from the official Limine protocol header and carry its 0BSD license notice.
