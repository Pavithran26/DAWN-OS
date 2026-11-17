# Phase C — Userspace and Storage Plan

## Status

Planning document only. Phase C runtime code is not enabled yet.

The current kernel has a Limine boot path, physical and virtual memory managers, ACPI/xAPIC/IOAPIC interrupt handling, a Local APIC timer, interrupt-frame context switching, and a single-CPU timer-preemptive kernel-thread scheduler. Phase B remains in progress until its BIOS and UEFI QEMU test matrix, timer calibration, and scheduler stress checks are verified. Phase C runtime code is not enabled.

Phase C must not be represented as complete or bootable until those prerequisites and the tests below exist.

## Current implementation snapshot (2026-11-17)

The first Phase C foundation increment is present on `main`:

- `include/syscall.h` defines ABI v1, syscall numbers, argument-register convention, return/error convention, and the 4 KiB write limit.
- `kernel/syscall.c` implements a kernel-callable dispatcher for ABI-version lookup and bounded serial `write` to descriptors 1 and 2. Unsupported calls return `ENOSYS`.
- `kernel/page_table.c` provides user-range validation and bounded copy helpers that require effective user mappings and, for writes into userspace, effective writable permissions.
- The page-table self-test creates a temporary user mapping and exercises copy-in/copy-out and invalid-range cases.
- The kernel test calls the dispatcher directly and checks ABI-version, descriptor, pointer, length, and unsupported-call errors.

**This is not yet a functioning userspace syscall path.** The dispatcher is called as a normal kernel C function by the self-test. IDT vector `0x80` is not installed; no TSS/RSP0, ring-3 transition, isolated process address space, process lifecycle, or ELF64 loader exists. The current page-table manager still operates on the active kernel root. Do not describe user programs as runnable.

The Phase B BIOS/UEFI validation gate also remains a prerequisite. No build, QEMU, or CI result is claimed by this source review.

## Implementation progress — GDT/TSS foundation (2026-11-17)

The kernel now has a dedicated GDT/TSS initialization increment:

- Installs kernel code/data and ring-3 code/data descriptors with documented selectors.
- Installs an available 64-bit TSS descriptor and loads TR.
- Provides an aligned 16 KiB privilege-transition kernel stack as TSS.RSP0, with an API for later per-process kernel stacks.
- Adds self-test coverage for TR selector, descriptor type/presence, RSP0 canonicality/alignment, and stack setter rejection/restore.
- Initialization runs before IDT setup. No user-mode transition is attempted.

**Still disabled:** no DPL3 syscall gate, ring-3 IRET frame, user task, process-specific CR3, process lifecycle, or ELF loader. The privilege stack is prepared only. BIOS/UEFI build and boot validation remain separate prerequisites; no execution result is claimed here.

## Goals

Build a small, auditable userspace environment with a documented privilege boundary and a simple, testable storage path. The initial target is QEMU x86_64 with BIOS and UEFI boot, using a virtio block device. Real hardware support comes later.

## Required order

1. **Finish Phase B foundations**
   - Physical frame allocator with allocation ownership, invalid/double-free rejection, and deterministic tests.
   - Page-table manager with explicit mapping permissions and address-space lifetime rules.
   - CPU exceptions, interrupt controller, timer, and interrupt-safe diagnostics.
   - Kernel threads, context switching, and a single-CPU scheduler.
   - Add SMP only after locking and per-CPU state are tested.

2. **Define the userspace ABI**
   - Enter ring 3 with a dedicated per-process address space and guarded user/kernel boundaries.
   - Define syscall numbers, argument registers, return/error conventions, and ABI versioning in a public header.
   - Validate every user pointer and length before accessing user memory.
   - Provide a minimal process lifecycle: create, execute, exit, wait, and terminate.
   - Keep kernel pointers and privileged operations out of userspace APIs.

3. **Bring up a minimal userspace runtime**
   - Start with a tiny statically linked ELF64 user program.
   - Validate ELF headers, program-header bounds, segment sizes, alignment, and address ranges before mapping.
   - Initially support a small documented subset of ELF64; reject unsupported relocations and malformed images.
   - Add a minimal init process that starts and supervises the first user task.
   - Do not introduce dynamic linking until the static executable path is stable.

4. **Add storage in layers**
   - Implement PCI device discovery sufficient to identify a supported virtio block device.
   - Add virtio block queue setup with DMA/buffer bounds and timeout/error handling.
   - Begin with a read-only block-device test path.
   - Add a small read-only filesystem image format for deterministic boot tests.
   - Add write support only after block I/O ordering, error recovery, and corruption tests are available.
   - Select a writable filesystem only after the read-only storage path is stable; do not claim ext4 compatibility prematurely.

## Security and correctness rules

- No user process may execute in ring 0.
- No syscall may trust a user-provided pointer, count, offset, or length.
- No storage write may happen before device bounds and request sizes are validated.
- No allocator may return reserved, firmware, framebuffer, kernel, or bootloader-owned memory.
- No bootloader-reclaimable memory is reclaimed until its lifetime is explicitly ended.
- Keep the first scheduler single-CPU; SMP requires synchronization and locking.
- Use deterministic synthetic tests for malformed ELF files, invalid syscalls, out-of-range block requests, short I/O, device errors, and filesystem corruption.
- Keep serial diagnostics available in headless VM tests.

## Acceptance criteria

- BIOS and UEFI QEMU boot tests remain green.
- A user-mode test executable runs at ring 3 and returns a known exit status through the syscall ABI.
- Invalid user pointers and malformed ELF images are rejected without kernel faults.
- A virtio block device is discovered and read with verified bounds.
- A read-only filesystem image can be mounted and a known file read byte-for-byte.
- Failure paths are tested; a successful compile alone is not acceptance.

## Explicitly out of scope for the first Phase C increment

- GUI desktop, terminal emulator, file manager, partition editor, and task manager.
- Networking and remote filesystems.
- Dynamic linking, package manager, writable production filesystem, and physical hardware claims.
