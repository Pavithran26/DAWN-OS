# Phase C status

## Process address-space foundation

- Added an address-space root that copies only the supervisor half (PML4 entries 256-511) from the boot kernel root; the lower half starts empty and is private to each address space.
- Added explicit user-page mapping and translation APIs operating on a supplied address-space root rather than the global active root.
- Added teardown for private lower-half page-table frames. User data frames remain caller-owned and are not freed by address-space teardown.
- Added self-test coverage for distinct roots, private user mappings, and teardown.
- Address-space activation is not yet connected to process execution. User execution remains disabled until CR3 switching is coordinated with interrupt entry, per-process kernel stacks, syscall return, and scheduler lifecycle.

## Remaining

- Review and validate address-space mapping/teardown under BIOS and UEFI QEMU.
- Add controlled activation and TLB handling.
- Integrate process-owned kernel stacks with scheduler/context-switch and interrupt/syscall entry paths.
- Add a safe process return/exit path, ELF64 loading, and a controlled ring-3 test.
- Phase B validation remains pending; do not mark Phase B complete until its documented timer/scheduler and BIOS/UEFI test matrix is satisfied.


## Process resource lifecycle (2026-11-17)

- The bounded process manager owns one isolated address-space root and one kernel privilege stack per process record.
- CREATED, READY, and EXITED transitions, metadata inspection, and resource reaping are implemented with CPL0 checks and single-CPU interrupt-disabled serialization.
- The four-entry process self-test is wired into the kernel test image. It has not been compiled or executed in this update.
- Process records are not runnable: there is no scheduler binding, CR3 activation, per-process stack switching, ELF loader, or user-mode exit path.
