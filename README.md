# DAWN-OS

DAWN-OS is an experimental, freestanding x86_64 operating-system project with a server-first direction. Its long-term goals are reliable boot and recovery, low request latency, high sustained concurrency, and efficient CPU and memory use. These are goals, not measured results.

## Current status

The repository now contains the first Phase A kernel scaffold:

- A 64-bit Limine entry path with a dedicated bootstrap stack.
- Limine request delimiters, a base-revision 6 tag, and a framebuffer request.
- Bounded COM1 serial diagnostics; serial output failure cannot trap the kernel in an infinite polling loop.
- A deterministic kernel self-test mode that reports PASS/FAIL over serial and uses QEMU's \`isa-debug-exit\` device when available.
- An ELF layout with separate executable, read-only, and writable load segments.

This is **not a complete operating system**. Physical/virtual memory management, exceptions, interrupt controllers, timer, scheduler, SMP, filesystem, userspace, syscall ABI, service supervisor, storage/network drivers, network stack, and server applications are not implemented. The kernel ELF build is not proof of a successful boot. ISO creation and BIOS/UEFI boot still require validation with the matching Limine binaries, xorriso, and QEMU.

## Build the kernel ELF

Requirements: GNU Make, GCC with x86_64 code-generation support, and GNU binutils (\`ld\` and \`readelf\`). From the repository root:

\`\`\`sh
make clean kernel
make check
file build/dawn-kernel.elf
\`\`\`

The build is freestanding: it disables hosted runtime assumptions, PIE/PIC, stack protector, the x86_64 red zone, and SIMD code generation. The default build uses GCC and GNU binutils from the host toolchain; a Linux-hosted compiler is only a build tool, not a DAWN-OS runtime.

A separate self-test kernel can be compiled with:

\`\`\`sh
make kernel-test
\`\`\`

## Build a BIOS/UEFI hybrid ISO

Obtain a matching Limine v12 binary package and place its \`limine\` installer, \`limine-bios.sys\`, \`limine-bios-cd.bin\`, \`limine-uefi-cd.bin\`, and \`BOOTX64.EFI\` under \`limine-binary/\` (or set \`LIMINE_DIR\` to that directory). Install \`xorriso\`, then run:

\`\`\`sh
make iso LIMINE_DIR=limine-binary
\`\`\`

The ISO target stages the kernel and Limine files, creates a hybrid BIOS/UEFI image, and runs \`limine bios-install\` on it. The build does not download or silently replace third-party bootloader binaries.

To create and run the self-test image:

\`\`\`sh
make qemu-test LIMINE_DIR=limine-binary
\`\`\`

This requires \`qemu-system-x86_64\` and \`timeout\`. QEMU must be able to use the \`isa-debug-exit\` device; exit status 33 means the kernel test reported success, 35 means it reported failure. For an interactive serial boot instead:

\`\`\`sh
make run LIMINE_DIR=limine-binary
\`\`\`

## Phase A remaining work

- Run the ISO build with the selected Limine v12 binaries and confirm its BIOS/UEFI layout.
- Boot under QEMU and retain serial output as test evidence.
- Validate both firmware paths where OVMF/UEFI firmware is available.
- Add further startup and request-validation tests before beginning memory management.

## Roadmap

1. **Boot and test:** verify Limine boot on QEMU, preserve serial diagnostics, and expand the kernel test harness.
2. **Memory and interrupts:** validate the boot memory map, implement a physical page allocator and virtual memory, then add exceptions, interrupt controllers, and a timer.
3. **Execution and SMP:** implement kernel threads, context switching, a scheduler, CPU startup, synchronization, and per-CPU state.
4. **Userspace and storage:** define a syscall ABI and privilege boundary, add a compact runtime and service supervisor, then add filesystem and virtio block I/O.
5. **Networking:** add virtio networking and a documented network stack, then implement a small reproducible HTTP workload.
6. **Measurement and optimization:** establish correctness baselines and measure throughput, p50/p95/p99 latency, CPU time per request, RAM, service-ready time, and failure recovery. Compare with Linux only under equivalent hardware/VM, resource limits, devices, service semantics, workload, configuration, and duration.

No performance or security superiority over Linux is claimed. A small kernel ELF is not directly comparable to a complete Linux distribution.

## Licensing

The project retains the repository's Unlicense dedication. The minimal Limine protocol declarations in \`include/limine_requests.h\` are derived from the official Limine protocol header and carry its 0BSD license notice.
