# Phase C status

## Process address-space foundation

- Added an address-space root that copies only the supervisor half (PML4 entries 256-511) from the boot kernel root; the lower half starts empty and is private to each address space.
- Added explicit user-page mapping and translation APIs operating on a supplied address-space root rather than the global active root.
- Added teardown for private lower-half page-table frames. User data frames remain caller-owned and are not freed by address-space teardown.
- Added self-test coverage for distinct roots, private user mappings, and teardown.
- Address-space activation is intentionally not exposed yet. User execution remains disabled until CR3 switching is coordinated with interrupt entry, per-process kernel stacks, syscall return, and process lifecycle.

## Remaining

- Review and validate address-space mapping/teardown under BIOS and UEFI QEMU.
- Add controlled activation and TLB handling.
- Add per-process kernel stacks and safe exception/syscall return.
- Add ELF64 loading, process lifecycle, and a controlled ring-3 test.
- Phase B validation remains pending; do not mark Phase B complete until its documented timer/scheduler and BIOS/UEFI test matrix is satisfied.


## Address-space activation foundation (2026-11-17)

- Added an explicit CPL0-only CR3 activation/deactivation API. Both operations reject calls while IF is set; callers must enter with interrupts disabled.
- The boot kernel PML4 is retained separately so new address spaces always inherit the supervisor-only upper half from the original kernel root, not from whichever process root is currently active.
- The address-space self-test now activates a private root, verifies its user mapping through the active-root translation path, restores the kernel root, and confirms the private mapping is no longer visible. Failure cleanup also restores the kernel root before releasing page-table frames.
- Activation does not launch ring 3, allocate a process kernel stack, or provide a return continuation. The existing TSS.RSP0 remains the bootstrap privilege stack. Do not use this as a process scheduler until per-process kernel stacks and lifecycle handling are implemented.
- No build, QEMU, or CI validation is claimed for this source update.
