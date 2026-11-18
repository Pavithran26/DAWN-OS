# Phase C status

## Process address-space foundation

- Added an address-space root that copies only the supervisor half (PML4 entries 256-511) from the boot kernel root; the lower half starts empty and is private to each address space.
- Added explicit user-page mapping and translation APIs operating on a supplied address-space root rather than the global active root.
- Added teardown for private lower-half page-table frames. User data frames remain caller-owned and are not freed by address-space teardown.
- Added self-test coverage for distinct roots, private user mappings, and teardown.
- Added CPL0/IF-disabled address-space activation, process-to-process switching, and kernel-root restoration. Scheduler dispatch now pairs each bound process address space with its TSS.RSP0 stack; process-bound tasks still execute as CPL0 kernel threads.

## Remaining

- Compile and run the expanded kernel test image; validate the controlled CPL3 ELF64 launch and `SYS_EXIT` return under BIOS and UEFI QEMU.
- Exercise user-origin timer preemption, user exception termination, syscall pointer validation, repeated process launch/reap, and loader rollback under runtime tests.
- Add a broader userspace runtime (argument/environment setup, wait/exec lifecycle, and more syscall coverage) before treating this as a general-purpose userspace environment.
- Phase B validation remains pending; do not mark Phase B complete until its documented timer/scheduler and BIOS/UEFI test matrix is satisfied.


## Process resource lifecycle (2026-11-17)

- The bounded process manager owns one isolated address-space root and one kernel privilege stack per process record.
- CREATED, READY, and EXITED transitions, metadata inspection, and resource reaping are implemented with CPL0 checks and single-CPU interrupt-disabled serialization.
- Scheduler threads can be bound one-to-one to READY process records. Dispatch switches CR3 and TSS.RSP0 between process contexts; exiting bound threads transition their process to EXITED after switching away.
- The process-manager and two-process scheduler self-tests are wired into the kernel test image. They have not been compiled or executed in this update.
- These are still CPL0 kernel-thread contexts, not user processes: no ELF loader, CPL3 entry/return path, or user-mode exit syscall exists.


## Controlled process activation (2026-11-17)

- `vm_address_space_activate` and `vm_address_space_activate_kernel` perform guarded CR3 switches only from CPL0 with interrupts disabled. Address-space activation refreshes shared supervisor PML4 entries before loading the process root.
- `process_switch_to` requires CPL0 with IF clear, verifies the current kernel stack, target privilege stack, and switch code are supervisor mappings, then switches CR3 and TSS.RSP0 with rollback on activation failure. `process_deactivate` restores the kernel root and bootstrap TSS stack.
- The scheduler binds at most one live kernel thread to each READY process and switches process contexts at dispatch boundaries. A process-bound thread that exits is marked EXITED only after its address space is no longer active.
- The process and scheduler self-tests cover process-to-process switching, kernel-context restoration, duplicate binding rejection, lifecycle transitions, and reaping.
- No build, emulator, or CI result is claimed. Ring-3 execution remains disabled.


## Scheduler-bound process contexts (2026-11-17)

- Kernel threads can be bound to a READY process before the scheduler starts running. A process may be bound to only one live kernel thread in this initial model.
- The scheduler now switches the active address-space CR3 and TSS.RSP0 when dispatching between process-bound threads and kernel-only threads. Exiting process-bound threads are marked EXITED after switching away from their address space.
- Process context switches are CPL0-only and execute with interrupts disabled. The current kernel stack, target process kernel stack, and switch code are checked as supervisor mappings before activation.
- Added a two-process round-robin self-test covering process A to B to A switching, yield/resume, exit, process-state transition, and resource reaping. The test is wired into the kernel test image.
- This is still kernel-thread execution using process-owned address spaces; it does not yet enter CPL3, load ELF images, or provide user-mode syscall/return handling.
- No compilation, QEMU, or CI validation is claimed for this source update.


## Interrupt-frame privilege validation (2026-11-17)

- Added `interrupt_frame_from_user` and `interrupt_frame_user_context_valid` so optional privilege-transition stack fields are only inspected after confirming a CPL3-origin frame.
- Validates user CS/SS selectors, lower-canonical RIP/RSP, fixed RFLAGS bit, and rejects IOPL/NT/VM states.
- Added synthetic positive and negative frame tests to the interrupt self-test.
- This does not yet provide a safe user-mode return path or prove CPL3 execution. Build and emulator validation remain pending.


## CPL3 syscall dispatch gate (2026-11-17)

- Interrupt dispatch now validates CPL3-origin frames and permits only vector 0x80 from user mode. All other user-origin traps are rejected through the fatal path until user-mode scheduling and exception handling are ready.
- Valid CPL3 syscall frames are passed to the bounded syscall dispatcher and returned through the common saved-frame/iretq epilogue. CPL0 syscall smoke tests remain supported.
- Added a synthetic CPL3 syscall dispatch test for ABI-version result placement. This is not a hardware ring-3 transition test.
- Ring-3 process launch is still not enabled: scheduler ownership of user trap stacks, user-mode timer preemption, recoverable user exceptions, and process exit are outstanding. Build, QEMU and CI validation remain pending.


## Mapped CPL3 return-context validation (2026-11-17)

- User-origin interrupt frames now require an executable user mapping for RIP and a writable user mapping immediately below the saved RSP.
- Added self-test mapping fixtures with allocated physical pages, permission-negative cases, and cleanup.
- Checks use the active address space. This hardens return-frame validation but does not implement user-mode scheduling, recoverable exceptions, or process exit.
- Compilation and emulator validation remain pending.


## Guarded user-entry context validation (2026-11-17)

- user_mode_validate_entry now requires a READY process to be active with its own CR3 root and TSS.RSP0 stack, CPL0, and interrupts disabled.
- The entry point must be user executable; the initial stack must be user writable and correctly aligned.
- Self-test now creates and activates a temporary process with private code/stack mappings, tests positive and negative entry parameters, then tears down the process and caller-owned frames.
- This validates launch preconditions only. Safe scheduler continuation, user timer preemption, recoverable user faults and process exit are still outstanding. Build and emulator checks remain pending.


## Static ELF64 executable validation (2026-11-17)

- Added ELF64 x86-64 ET_EXEC structural validation for bounded static executable images and load-segment metadata.
- Enforces file and memory bounds, user virtual range, power-of-two/congruent alignment, W^X, executable entry containment, and page-level non-overlap. Dynamic/interpreter images are rejected.
- Added synthetic valid/invalid image self-tests and wired them into the kernel test image.
- This is validation only: no segment allocation/copy, BSS zeroing, relocation, process image ownership, or actual ELF launch is implemented. Compilation and emulator validation remain pending.


## Scheduler-managed CPL3 trap path (2026-11-18)

- Added a process-bound scheduler thread configuration API that builds a CPL3 IRETQ context on the process privilege stack after validating executable user RIP and writable user RSP.
- The scheduler validates CPL3 frames against the process-owned kernel stack while retaining existing kernel-thread stack checks for CPL0 frames.
- User-origin syscall and APIC timer entries are routed through the process-aware path. SYS_EXIT and recoverable user exceptions switch away from the faulting thread; NMI, double fault and machine check remain fatal.
- Active-root virtual translation makes existing user-range/copy helpers operate on the process that received the syscall.
- Synthetic self-tests cover the initial user frame and its mappings, but no CPL3 instruction stream is launched. Build, QEMU and CI validation remain pending.


## ELF64 loading and controlled first user launch (2026-11-18)

- Added process-owned user-page allocation, kernel-side address-space read/write/zero helpers, and user-page unmapping for rollback and teardown.
- The ELF loader now allocates PT_LOAD pages with their final user permissions, zeroes complete mapped pages before loading, copies file contents, zeroes BSS, and builds a four-page RW/NX user stack. Images exceeding 256 total user pages are rejected.
- Reaping releases all process-owned user frames and their private page tables. Failed loads roll back pages allocated since the process checkpoint.
- Loader tests verify executable bytes, RX permissions, zeroed BSS, RW/NX stack permissions, and allocator-frame recovery after reaping.
- Added a test-only ELF64 user program that calls `SYS_WRITE`, verifies the result in user code, then issues `SYS_EXIT`; the scheduler test now attempts a real CPL3 launch and return through the syscall gate. This is wired into the kernel test image but not yet compiled or run.
- Normal boot does not launch a user process. Phase B validation and the BIOS/UEFI QEMU test matrix remain outstanding.


## Validation boundary (2026-11-18)

- These changes are source-level implementation only. The kernel and test image have not been compiled in this increment.
- The new CPL3 test program and `SYS_EXIT` path are wired into the kernel test target, but no QEMU run has confirmed the IRETQ transition or the return to the bootstrap frame.
- Do not treat a wired test, ELF validation, or successful source push as evidence that DAWN-OS has booted a real userspace process.


## CPL3 timer-preemption coverage (2026-11-18)

- The test ELF now performs a bounded user-mode spin before issuing SYS_WRITE and SYS_EXIT.
- scheduler_elf_launch_self_test runs with the calibrated periodic LAPIC timer and a READY kernel peer. It requires the timer to preempt the user thread, the peer to execute, and the user thread to resume and exit through the syscall path.
- Assertions cover timer-preemption counters, peer progress, user-exit accounting, process exit and reaping. Timer cleanup is included on failure paths.
- This remains uncompiled/unexecuted source until the BIOS and OVMF test matrix is run.


## Userspace identity and cooperative yield (2026-11-18)

- Extended ABI v1 with `SYS_GETPID` and `SYS_YIELD` without renumbering existing calls. `SYS_GETPID` reports the active process identifier; `SYS_YIELD` is handled only from a validated CPL3 syscall frame and returns zero when the task resumes.
- The scheduler preserves the interrupted user frame, marks the process-bound thread READY, and dispatches another READY task. User-yield syscalls have separate accounting from timer preemptions and kernel-thread yields.
- The synthetic CPL3 dispatch test checks process identity. The test ELF checks that GETPID is non-zero, requires a zero YIELD result, then writes its success marker and exits. Its kernel peer yields once so it remains available for timer-driven preemption as well.
- This is source-level coverage only. The new syscall path, user-mode cooperative yield, timer interaction, and BIOS/UEFI boot matrix have not been compiled or executed in this increment.

## User process exit status (2026-11-18)

- The userspace ABI defines the first argument in RDI as the `SYS_EXIT` status.
- The CPL3 scheduler exit path records that signed 64-bit status against the active process before marking its kernel thread dead.
- Process metadata exposes the recorded status after the scheduler switches away and marks the process EXITED; the status remains available until the process is reaped.
- This is exit-status recording only. A blocking `WAIT` syscall, parent/child relationships, and wait-queue wakeups are not implemented by this increment.
- Source implementation only; no compilation, emulator, BIOS/UEFI, or CI tests were run.

## Parent/child process metadata (2026-11-18)

- A process created while another process address space is active records that active process as its parent. Kernel-root process creation produces a root process with parent ID 0.
- `process_get_child_info` returns child metadata only when the requested parent-child relationship matches.
- Reaping a parent is rejected while any child record remains, including an EXITED child. Children must be reaped before the parent record can be released.
- This is lifecycle groundwork only. User-mode process creation, parent termination/orphan adoption policy, and blocking `WAIT` are not implemented yet.
- Added source-level self-test coverage for parent capture, child lookup, exited-child visibility, and child-before-parent reaping.
- No compilation, QEMU, BIOS/UEFI, or CI tests were run for this increment.


## Parent-side child collection groundwork (2026-11-18)

- Added a CPL0-only child collection primitive that accepts only an EXITED child owned by the specified READY parent.
- Collection can copy the signed 64-bit exit status into an aligned, writable user-memory slot in the parent's own address space, without switching away from the currently active address space.
- The child is reaped before its status is published. Collection is rejected if the child still owns unreaped child records or if the destination is not writable user memory.
- This is the process-manager primitive for SYS_WAIT; it does not yet block a running parent or wake it when a child exits. The user syscall remains unimplemented.
- Source-only update. No compilation, QEMU, BIOS/UEFI, or CI tests were run.

## Full blocking SYS_WAIT and scheduler wakeup semantics (2026-11-18)

- Extended syscall ABI v1 with `SYS_WAIT` (number 5). Accepts target child PID in RDI (or <= 0 for any child) and user status address in RSI.
- Added `process_has_children` to inspect whether a parent owns matching child records and whether any has already transitioned to `PROCESS_STATE_EXITED`.
- Extended the scheduler with a `THREAD_BLOCKED` state. If a matching child exists but has not exited, the calling parent thread is placed in `THREAD_BLOCKED` with its target child filter and user status destination recorded.
- When an exiting child process calls `SYS_EXIT` or faults, the scheduler identifies any thread blocked waiting for that child (or any child of that parent) and transitions it to `THREAD_READY`.
- During context switch away from the dying child, the child is marked `PROCESS_STATE_EXITED`, the parent collects the child, its status is written into the parent's user address space, the child is reaped, and the child PID is returned in RAX when the parent resumes.
- If a child has already exited at the time `SYS_WAIT` is called, it is collected immediately without blocking. If no matching children exist, `SYS_WAIT` immediately returns `-ECHILD`.
- Added `scheduler_wait_self_test` verifying parent child creation, yield, child exit, collection, and parent notification.
- Source-only increment. No compilation, QEMU, BIOS/UEFI, or CI test execution was run.

## File descriptor table, process spawning, userspace runtime, and Init process (2026-11-18)

- Implemented per-process file descriptor table (16 descriptors per process). Standard descriptors 0 (stdin, read-only), 1 (stdout, write-only), 2 (stderr, write-only) are automatically configured on process creation, and child processes inherit parent descriptor tables.
- Implemented file descriptor lifecycle operations: `process_fd_get`, `process_fd_set`, `process_fd_allocate`, `process_fd_close`, `process_fd_dup`, `process_fd_dup2`.
- Extended syscall dispatcher with `SYS_READ`, `SYS_CLOSE`, `SYS_DUP`, `SYS_SPAWN`, and `SYS_EXEC`.
- Implemented `elf64_setup_user_stack` laying out arguments, environment strings, NULL-terminated pointer arrays, `argc`, 16-byte aligned RSP, and initial register conventions (RDI=argc, RSI=argv, RDX=envp) according to System V AMD64 ABI specifications.
- Implemented dynamic process spawning and image execution (`process_spawn`, `kernel_thread_configure_user_args`, `scheduler_user_spawn`, `scheduler_user_exec`).
- Created the userspace runtime foundation in `user/`: `user/include/dawn.h` (userspace syscall wrappers and types), `user/crt0.S` (C runtime startup), and `user/libdawn.c` (userspace helper routines).
- Implemented the native DAWN-OS Init process (`user/init.c`) and embedded executable generator `elf64_build_init_image`, launching PID 1 under the scheduler.
- Phase C source implementation items are complete. All additions include source-level self-tests.
- Source-only increment. No compilation, QEMU, BIOS/UEFI, or CI tests were run.
