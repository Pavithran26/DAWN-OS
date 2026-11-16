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

This is **not a complete operating system**. Physical/virtual memory management, exceptions, interrupt controllers, timer, scheduler, SMP, filesystem, userspace, syscall ABI, service supervisor, storage/network drivers, network stack, and server applications are not implemented. An ELF build or ISO creation is not proof of a successful boot; check the CI run logs and artifacts for boot evidence.

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

## Phase A remaining work

- Confirm CI BIOS and UEFI QEMU runs and review retained serial output.
- Validate on additional QEMU machine/firmware configurations.
- Add startup and boot-request validation tests before beginning memory management.

## Roadmap

1. **Boot and test:** verify Limine boot on QEMU, preserve serial diagnostics, and expand the kernel test harness.
2. **Memory and interrupts:** validate the boot memory map, implement a physical page allocator and virtual memory, then add exceptions, interrupt controllers, and a timer.
3. **Execution and SMP:** implement kernel threads, context switching, a scheduler, CPU startup, synchronization, and per-CPU state.
4. **Userspace and storage:** define a syscall ABI and privilege boundary, add a compact runtime and service supervisor, then add filesystem and virtio block I/O.
5. **Networking:** add virtio networking and a documented network stack, then implement a small reproducible HTTP workload.
6. **Measurement and optimization:** establish correctness baselines and measure throughput, p50/p95/p99 latency, CPU time per request, RAM, service-ready time, and failure recovery. Compare with Linux only under equivalent hardware/VM, resource limits, devices, service semantics, workload, configuration, and duration.

No performance or security superiority over Linux is claimed. A small kernel ELF is not directly comparable to a complete Linux distribution.

## Licensing

The project retains the repository's Unlicense dedication. The minimal Limine protocol declarations in `include/limine_requests.h` are derived from the official Limine protocol header and carry its 0BSD license notice.
