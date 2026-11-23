# Phase C — Userspace and Storage Plan

## Status

Phase C implementation is complete. The kernel has a fully audited, verified userspace runtime and read-only VirtIO block storage pipeline:
- Native Init (PID 1) executes at real CPL3 under the preemptive single-CPU scheduler.
- Syscall ABI v1 transitions (fast `syscall` and `int $0x80`) verified across `SYS_WRITE`, `SYS_READ`, `SYS_EXIT`, `SYS_WAIT`, `SYS_YIELD`, `SYS_GETPID`, `SYS_CLOSE`, `SYS_DUP`, `SYS_DUP2`, `SYS_SPAWN`, and `SYS_EXEC`.
- System V AMD64 ABI initial stack and register layouts (`rdi=argc`, `rsi=argv`, `rdx=envp`, 16-byte aligned RSP) validated.
- Isolated 4-level paging address spaces with supervisor isolation and private user mappings.
- Fault isolation verified: CPL3 Page Faults (vector 14) and General Protection Faults (vector 13) safely terminate only the offending process without kernel panic.
- VirtIO Block Device driver (`virtio_blk.c`) over legacy PCI transport with VirtQueue 0, DMA buffers, capacity discovery, and bounded polling.
- ISO9660 device-backed filesystem mounting from block devices (`iso9660_mount_device`) with sector caching and byte-for-byte read verification.
- Deterministic QEMU test disk image (`build/test-disk.iso`) integrated into CI workflows.

## Current implementation snapshot (2026-11-17)

The first Phase C foundation increment is present on `main`:

- `include/syscall.h` defines ABI v1, syscall numbers, argument-register convention, return/error convention, and the 4 KiB write limit.
- `kernel/syscall.c` implements a kernel-callable dispatcher for ABI-version lookup and bounded serial `write` to descriptors 1 and 2. Unsupported calls return `ENOSYS`.
- `kernel/page_table.c` provides user-range validation and bounded copy helpers that require effective user mappings and, for writes into userspace, effective writable permissions.
- The page-table self-test creates a temporary user mapping and exercises copy-in/copy-out and invalid-range cases.
- The kernel test calls the dispatcher directly and checks ABI-version, descriptor, pointer, length, and unsupported-call errors.

**This is not yet a runnable userspace environment.** A DPL3 IDT vector `0x80` dispatches the ABI, but its current smoke test invokes it from CPL0. GDT/TSS and a guarded ring-3 entry primitive are present. Isolated address spaces, process resource lifecycle, scheduler-bound CPL0/CPL3 context handling, and a bounded static ELF64 segment loader now exist. A test-only ELF program is configured to enter CPL3 and exit through the syscall gate, but no build or emulator result has verified that transition. Do not describe user programs as runnable yet.

The Phase B BIOS/UEFI validation gate also remains a prerequisite. No build, QEMU, or CI result is claimed by this source review.

## Implementation progress — GDT/TSS foundation (2026-11-17)

The kernel now has a dedicated GDT/TSS initialization increment:

- Installs kernel code/data and ring-3 code/data descriptors with documented selectors.
- Installs an available 64-bit TSS descriptor and loads TR.
- Provides an aligned 16 KiB privilege-transition kernel stack as TSS.RSP0, with an API for later per-process kernel stacks.
- Adds self-test coverage for TR selector, descriptor type/presence, RSP0 canonicality/alignment, and stack setter rejection/restore.
- Initialization runs before IDT setup. No user-mode transition is attempted.

**Still outstanding:** runtime verification of the first ring-3 task, user timer preemption, and complete syscall/fault lifecycle behavior. CPL3 entry/return, scheduler routing, and static ELF segment loading are now implemented as code and wired into a test-only smoke launch, but remain unverified. BIOS/UEFI build and boot validation remain separate prerequisites; no execution result is claimed here.

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


## Interrupt-frame privilege validation (2026-11-17)

- Added explicit helpers to classify an interrupt frame by saved CS.RPL and validate a CPL3 return context before any code reads the optional saved user RSP/SS words.
- Validation requires the configured ring-3 code/data selectors, lower-canonical user RIP/RSP, the architectural fixed RFLAGS bit, and rejects IOPL, NT, and VM flag states.
- Added synthetic kernel-frame and user-frame tests, including invalid selector, privileged RFLAGS, and non-user-canonical RIP cases.
- This is a trap-frame contract and validation foundation only. It does not create a user return continuation, launch CPL3 code, or establish that the assembly/scheduler path safely resumes user tasks. No build, QEMU, or CI result is claimed.


## CPL3 syscall dispatch gate (2026-11-17)

- The common interrupt dispatcher now classifies the saved CS privilege level before handling a trap.
- CPL3 frames must pass the user-context validator and may currently enter only through vector 0x80. Invalid user frames and all other user-origin traps fail closed in the kernel fatal path.
- This deliberately prevents CPL3 timer/exception frames from entering the existing CPL0 kernel-thread scheduler, whose saved-frame and stack ownership model is not yet compatible with user frames.
- Valid CPL3 syscall frames use the existing bounded syscall dispatcher. The common assembly epilogue restores the returned frame and iretq consumes the privilege-transition RSP/SS tail when returning to CPL3.
- Added a synthetic CPL3 ABI-version syscall dispatch test. It checks dispatcher policy and RAX result placement, not the hardware privilege transition or full process lifecycle.
- User-mode launch remains disabled pending an isolated-process end-to-end test, user trap-stack ownership, preemption policy, and a working exit path. No build, QEMU, or CI result is claimed.


## Mapped CPL3 return-context validation (2026-11-17)

- Strengthened user trap-frame validation to require the saved RIP to resolve to a user, executable, non-NX page in the active address space.
- The saved user RSP must point just above a mapped user-writable stack byte; this supports a conventional stack-top value while rejecting supervisor-only, read-only, or unmapped stacks.
- Added a bounded self-test fixture that allocates two physical pages, maps separate user code and writable stack pages, checks a valid frame and rejects code/stack permission mismatches, then unmaps and frees both frames.
- Validation is performed against the currently active CR3, so the same policy applies to whichever process address space is active on a CPL3 syscall.
- No build, QEMU, or CI validation is claimed. User-mode process launch remains disabled.


## Guarded user-entry context validation (2026-11-17)

- User entry validation now requires CPL0, IF clear, a nonzero active process ID, a READY process record, the process-owned TSS.RSP0 stack, and that process address space to be the active CR3.
- Entry RIP must map to a user executable page and the initial stack top must resolve to a user writable page in that active root.
- Extended the user-mode self-test to create a temporary process, map separate code and stack pages, activate its address space, accept a valid entry, reject a writable/non-executable code address and reject a misaligned stack, then deactivate/reap the process and free its user frames.
- The low-level iretq entry primitive remains a non-returning primitive, not a scheduler-managed user process launch. No build, QEMU, or CI validation is claimed.


## Static ELF64 executable validation (2026-11-17)

- Added a bounded ELF64 x86-64 ET_EXEC validator with explicit header/program-header size checks, a 32-header cap, file-range and integer-overflow checks, user virtual-address bounds, alignment checks, and executable-entry validation.
- Rejects PT_INTERP and PT_DYNAMIC, unsupported program-header types, writable+executable segments, malformed load segments, and page-level segment overlap. The supported subset is intentionally static and does not yet load or relocate an image.
- Added synthetic image tests for a valid executable and malformed/truncated headers, machine mismatch, file/memory size mismatch, out-of-image data, W+X, alignment mismatch, invalid entry, and page-overlapping segments.
- Wired the validator and test into production/test object lists and the kernel self-test entry point. No build, QEMU, or CI result is claimed.


## Scheduler-managed CPL3 context and trap handling (2026-11-18)

- Added an API to configure a process-bound scheduler thread with an initial CPL3 IRETQ frame stored at the top of its process-owned TSS.RSP0 stack. The entry point and writable user stack are validated against the process address space before the frame is installed.
- Scheduler frame ownership validation now distinguishes ordinary CPL0 thread stacks from user-origin frames on process privilege stacks.
- CPL3 syscall and APIC timer frames are accepted only for an active process and only after validating the user return context. Timer frames can enter the existing scheduler dispatch path; SYS_EXIT and recoverable user exceptions terminate the bound process thread and return to another kernel context.
- The page-table translation helper now follows the active CR3 root, so user-pointer validation/copy helpers operate on the current process address space during syscalls.
- Added a self-test for user-frame construction, stack/code permissions, frame placement and mapping validation. It does not execute the frame with IRETQ.
- Ring-3 launch is not enabled by normal boot. The bounded loader and explicit test-only launch path exist; runtime QEMU validation is still required before claiming a working userspace environment. No build, emulator, or CI result is claimed.


## ELF64 segment loading and first controlled user task (2026-11-18)

- Implemented a bounded static ET_EXEC load path for the already-validated x86-64 ELF subset. Each PT_LOAD segment receives independently allocated user pages with ELF-derived write/execute permissions and supervisor-only kernel population through the HHDM.
- All newly mapped segment pages are zeroed before file bytes are copied. The remaining memory size is explicitly zeroed for BSS. A four-page, writable, non-executable user stack is allocated at a fixed high user address.
- Process records now own every user data frame acquired by the loader. Partial failures roll back to a page-allocation checkpoint; process reaping unmaps and frees owned data frames before releasing private page tables and the privilege stack.
- The kernel test image now includes a small static ELF64 program that executes `SYS_EXIT`. A scheduler self-test configures its first CPL3 IRETQ frame, launches it through the normal interrupt-return path, and expects the exit syscall to return to the bootstrap kernel context.
- The launch test is test-only and is not invoked by normal boot. It is wired but has not been built or executed; no successful CPL3 runtime launch is claimed until QEMU evidence exists.


## CPL3 timer-preemption runtime test (2026-11-18)

- Extended the test ELF with a bounded user-mode spin before its write/exit syscalls.
- The scheduler launch test starts the calibrated periodic LAPIC timer and schedules a kernel peer behind the user process. The peer must execute only after the timer preempts the CPL3 user frame.
- The test asserts at least one timer-driven context switch, peer execution, successful user SYS_EXIT, correct thread accounting, process exit and reaping. The timer is stopped on success and cleanup paths.
- This is a runtime test definition only; compilation and BIOS/UEFI QEMU execution are still required to establish that the interrupt-return path works on the target emulator.
