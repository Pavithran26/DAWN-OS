# Phase C — Userspace and Storage Plan

## Status

Planning document only. Phase C runtime code is not enabled yet.

The current kernel has a Limine boot path, memory-map validation, and HHDM response validation. Phase B is still incomplete: physical page allocation, virtual-memory/page-table management, exception and interrupt handling, timer support, kernel threads, and a scheduler remain prerequisites.

Phase C must not be represented as complete or bootable until those prerequisites and the tests below exist.

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
