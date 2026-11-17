# Phase C status

## Process address-space foundation

- Added an address-space root that copies only the supervisor half (PML4 entries 256-511) from the boot kernel root; the lower half starts empty and is private to each address space.
- Added explicit user-page mapping and translation APIs operating on a supplied address-space root rather than the global active root.
- Added teardown for private lower-half page-table frames. User data frames remain caller-owned and are not freed by address-space teardown.
- Added self-test coverage for distinct roots, private user mappings, and teardown.
- Added CPL0/IF-disabled address-space activation and kernel-root restoration. Process activation pairs the address-space root with the process-owned TSS.RSP0 stack. This is not scheduler integration; the scheduler still handles kernel threads only.

## Remaining

- Review and validate address-space mapping/teardown under BIOS and UEFI QEMU.
- Integrate the activation boundary with scheduler/context-switch and interrupt/syscall entry paths.
- Add a safe process return/exit path, ELF64 loading, and a controlled ring-3 test.
- Phase B validation remains pending; do not mark Phase B complete until its documented timer/scheduler and BIOS/UEFI test matrix is satisfied.


## Process resource lifecycle (2026-11-17)

- The bounded process manager owns one isolated address-space root and one kernel privilege stack per process record.
- CREATED, READY, and EXITED transitions, metadata inspection, and resource reaping are implemented with CPL0 checks and single-CPU interrupt-disabled serialization.
- The four-entry process self-test is wired into the kernel test image. It has not been compiled or executed in this update.
- Process records are not runnable: there is no scheduler binding, CR3 activation, per-process stack switching, ELF loader, or user-mode exit path.


## Controlled process activation (2026-11-17)

- `vm_address_space_activate` and `vm_address_space_activate_kernel` perform guarded CR3 switches only from CPL0 with interrupts disabled. Address-space activation refreshes shared supervisor PML4 entries before loading the process root.
- `process_activate` requires a READY process, verifies the current kernel stack, activation code, and process privilege stack are mapped supervisor-only, then activates its CR3 root and TSS.RSP0. `process_deactivate` restores the kernel root and bootstrap TSS stack.
- Active-process records cannot be marked EXITED or reaped. Only one process may be active through this early API; there is no scheduling integration yet.
- The process self-test now checks successful activation/deactivation and rejects overlapping activation and teardown while active. The previous null-output self-test condition was corrected.
- No build, emulator, or CI result is claimed. Ring-3 execution remains disabled.


## Scheduler-bound process contexts (2026-11-17)

- Kernel threads can be bound to a READY process before the scheduler starts running. A process may be bound to only one live kernel thread in this initial model.
- The scheduler now switches the active address-space CR3 and TSS.RSP0 when dispatching between process-bound threads and kernel-only threads. Exiting process-bound threads are marked EXITED after switching away from their address space.
- Process context switches are CPL0-only and execute with interrupts disabled. The current kernel stack, target process kernel stack, and switch code are checked as supervisor mappings before activation.
- Added a two-process round-robin self-test covering process A to B to A switching, yield/resume, exit, process-state transition, and resource reaping. The test is wired into the kernel test image.
- This is still kernel-thread execution using process-owned address spaces; it does not yet enter CPL3, load ELF images, or provide user-mode syscall/return handling.
- No compilation, QEMU, or CI validation is claimed for this source update.
