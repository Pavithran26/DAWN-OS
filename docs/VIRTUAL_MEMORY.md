# x86_64 virtual-memory and page-table manager

## Status

Implemented as an early Phase B component. It attaches to the currently active Limine root; it does not create process address spaces or replace CR3.

## Paging contract

- DAWN-OS explicitly requests Limine x86_64 four-level paging (PML4 / PDPT / PD / PT) and checks the response.
- Initialization checks CR0.PG/WP, CR4.PAE, rejects CR4.LA57, and checks EFER.LMA/NXE plus CPU NX support.
- The physical-address mask is derived from CPUID leaf `0x80000008`; unsupported widths are rejected.
- The current CR3 root is preserved. Existing Limine mappings are not discarded.
- HHDM is discovered at runtime. New page-table pages are allocated from USABLE RAM and accessed through HHDM; the active Limine page tables are in bootloader-reclaimable memory, which the protocol maps in HHDM for base revision 4+.

## API and permissions

- `vm_manager_initialize(hhdm_offset)` attaches to the active root.
- `vm_map_page(va, pa, flags)` maps one aligned 4 KiB page only when the physical frame is currently owned by the physical allocator.
- `vm_translate(va, &pa, &flags)` software-walks the tables and reports effective physical address and write/user/execute/NX permissions. It also understands existing 1 GiB and 2 MiB mappings for translation.
- `vm_unmap_page(va)` removes a 4 KiB leaf mapping and performs local `invlpg`. It does not implicitly free the mapped physical frame.
- Write/user permissions are checked at every ancestor. The manager refuses to silently relax an existing parent permission.
- Non-executable is the default. Executable pages require explicit `VM_PAGE_EXECUTE`; initialization requires NXE.

Missing intermediate tables are allocated, zeroed before publication, and connected into the active hierarchy. Updates use a memory fence and local TLB invalidation.

## Conservative limits

- Four-level paging only; five-level paging is not supported by this increment.
- Physical allocator tracking is currently limited to frames below 4 GiB.
- Only allocator-owned RAM frames may be passed to `vm_map_page`. Device/MMIO mappings require a separate cache-aware API.
- Large-page splitting/promotion are not implemented. Mapping through an existing large-page entry is rejected.
- Empty intermediate tables are retained after unmap. No ownership-aware table reclamation is attempted yet.
- No process address spaces, user-mode transition, copy-on-write, fault recovery, per-CPU TLB shootdowns, or SMP support.
- This manager is not safe for concurrent calls.

## QEMU self-tests

The self-test searches for actually unmapped lower-half pages, accommodating differences in the BIOS and UEFI firmware page-table layouts, so it does not overwrite existing Limine mappings. It allocates a physical frame, maps it writable/NX, checks translation and effective permissions, rejects duplicate and malformed mappings, unmaps it, verifies that translation is absent, and frees the data frame. It then checks a read-only executable mapping and tears it down. Intermediate tables are intentionally retained.

BIOS and UEFI logs must contain `DAWN-TEST: PASS: virtual-memory map/translate/unmap tests`. These tests do not prove user/supervisor isolation or a privilege transition.
