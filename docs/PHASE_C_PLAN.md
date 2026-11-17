# Phase C — Userspace and Storage Plan

## Status

Phase C foundation code is in progress. The kernel has ABI/syscall, GDT/TSS, ring-3 entry validation, isolated address-space, privilege-stack, and process-resource lifecycle primitives. No user process has been launched; this is not yet a runnable userspace environment.

The current kernel also has physical and virtual memory managers, ACPI/xAPIC/IOAPIC interrupt handling, a Local APIC timer, interrupt-frame context switching, and a single-CPU timer-preemptive kernel-thread scheduler. Phase B remains in progress until its BIOS and UEFI QEMU test matrix, timer calibration, and scheduler stress checks are verified.

Phase C must not be represented as complete or bootable until the prerequisites and acceptance tests below exist.

## Current implementation snapshot (2026-11-17)

The first Phase C foundation increment is present on `main`:

- `include/syscall.h` defines ABI v1, syscall numbers, argument-register convention, return/error convention, and the 4 KiB write limit.
- `kernel/syscall.c` implements a kernel-callable dispatcher for ABI-version lookup and bounded serial `write` to descriptors 1 and 2. Unsupported calls return `ENOSYS`.
- `kernel/page_table.c` provides user-range validation and bounded copy helpers that require effective user mappings and, for writes into userspace, effective writable permissions.
- The page-table self-test creates a temporary user mapping and exercises copy-in/copy-out and invalid-range cases.
- The kernel test calls the dispatcher directly and checks ABI-version, descriptor, pointer, length, and unsupported-call errors.

**This is not yet a runnable userspace environment.** A DPL3 IDT vector `0x80` dispatches the ABI, but its current smoke test invokes it from CPL0. GDT/TSS and a guarded ring-3 entry primitive are present. Isolated address-space and process resource lifecycle primitives now exist, including a controlled CPL0 CR3/TSS.RSP0 activation boundary. Scheduler integration, a safe user return path, and an ELF64 loader are still absent. No ring-3 program has been run. Do not describe user programs as runnable.

The Phase B BIOS/UEFI validation gate also remains a prerequisite. No build, QEMU, or CI result is claimed by this source review.

## Implementation progress — GDT/TSS foundation (2026-11-17)

The kernel now has a dedicated GDT/TSS initialization increment:

- Installs kernel code/data and ring-3 code/data descriptors with documented selectors.
- Installs an available 64-bit TSS descriptor and loads TR.
- Provides an aligned 16 KiB privilege-transition kernel stack as TSS.RSP0, with an API for later per-process kernel stacks.
- Adds self-test coverage for TR selector, descriptor type/presence, RSP0 canonicality/alignment, and stack setter rejection/restore.
- Initialization runs before IDT setup. No user-mode transition is attempted.

**Still outstanding:** no executed ring-3 task, CPL3 interrupt/syscall return path, or ELF loader. The privilege stacks are owned by process records and TSS.RSP0 is switched with scheduler-bound process contexts, but no CPL3 code has used them. BIOS/UEFI build and boot validation remain separate prerequisites; no execution result is claimed here.

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


## Syscall interrupt-gate foundation (2026-11-17)

- Installed vector `0x80` as a present, DPL3 64-bit interrupt gate.
- Added an assembly entry stub that normalizes the vector/error-code prefix and reuses the common register-save/`iretq` path.
- The interrupt dispatcher maps the documented x86-64 register ABI into `syscall_dispatch` and returns the result in RAX.
- The interrupt frame type documents optional saved RSP/SS fields that exist only after a privilege transition; same-CPL handlers must not read them.
- Added a CPL0 software-interrupt smoke test for ABI-version dispatch and gate attributes.

This enables interrupt-gate dispatch but does not prove ring-3 execution. A ring-3 caller, per-process kernel stack switching, user-mode return path, and process isolation remain outstanding. No build, emulator, or CI validation is claimed for this source change.


## Ring-3 entry primitive (2026-11-17)

- Added a low-level IRETQ transition routine using the configured ring-3 code/data selectors.
- Added validation that the entry point is mapped user-accessible and executable, and that the initial stack is user-accessible, writable, canonical and 16-byte aligned.
- Added negative argument-validation self-tests and included the new source in production/test link targets.

The primitive is deliberately not invoked by boot. It uses the currently active page-table root, which is still the kernel root; therefore it is not yet safe to launch an untrusted program. It also has no process-specific return continuation or exit/wait lifecycle. The next required work is isolated address-space creation and a controlled user-task return path before any ring-3 smoke program is launched. No build, QEMU, or CI result is claimed.


## Per-process privilege-stack pool foundation (2026-11-17)

- Added a bounded pool of four 16 KiB, 16-byte-aligned kernel privilege stacks, reserved independently of the bootstrap TSS stack.
- Added allocate, activate and release APIs. Activation accepts only the bootstrap stack or a currently allocated pool stack; release rejects the currently active TSS.RSP0 stack.
- The GDT/TSS self-test now exercises two distinct allocations, activation, active-stack release rejection, restoration of the bootstrap stack, release, and stale-handle rejection.
- The process resource manager assigns these stacks to process records, and scheduler dispatch updates TSS.RSP0 when switching process contexts. This does not yet permit safe ring-3 execution. Callers must serialize operations with interrupts disabled on the current single CPU.
- No build, QEMU, or CI validation is claimed for this source update.


## Process resource lifecycle foundation (2026-11-17)

- Added a bounded four-entry process table with monotonically increasing process IDs and explicit CREATED, READY, and EXITED states.
- Process creation now owns one isolated lower-half address-space root and one privilege-transition kernel stack. Partial allocation failure rolls back the address-space allocation.
- Added state transitions, metadata inspection, and reaping. Reaping releases private page-table frames and the process kernel stack; VM teardown refuses to free the active CR3 root.
- Operations are CPL0-only and serialize their table/resource updates with interrupts disabled on the current single CPU.
- This is not yet a runnable process subsystem: process-bound kernel threads can switch CR3/TSS.RSP0, but there is no CPL3 process entry/return path, ELF loader, or user exit/wait syscall. Process resources must not be used to launch ring-3 programs yet.
- Added deterministic self-test coverage for table capacity, unique IDs, state transitions, invalid transitions, resource cleanup, stale IDs, and slot reuse.
- No build, QEMU, or CI validation is claimed for this source update.


## Controlled process address-space activation (2026-11-17)

- Added explicit address-space activate and kernel-root restore APIs. Calls require CPL0 and IF=0; a CR3 reload changes the active root and refreshes shared supervisor PML4 entries before activation.
- Process switching is restricted to READY records and binds each isolated CR3 root to its allocated TSS.RSP0 privilege stack. The current kernel stack, target privilege stack, and switch code are verified as supervisor mappings in the target address space.
- The scheduler can now switch between process-bound kernel threads, including process A to B to A transitions, and restore the bootstrap kernel root when returning to kernel-only work. Exited process records are reaped only after switching away.
- The process and scheduler self-tests cover activation, switching, TSS stack association, duplicate binding rejection, active-process exit/reap rejection, kernel-context restoration, and cleanup.
- This is still a CPL0-only context-switch foundation. No ring-3 process has been executed, no ELF loader exists, and no user-mode syscall return path is enabled.
- No build, QEMU, or CI validation is claimed for this source update.
