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
