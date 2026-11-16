# Physical page-frame allocator

## Status
Implemented as an early Phase B component. It is a physical-frame allocator only, not a virtual-memory manager or kernel heap.

## Frame policy
- 4 KiB pages; tracks physical addresses below 4 GiB in this first version.
- Physical frame zero is permanently excluded.
- Only Limine `USABLE` entries enter the free pool.
- Bootloader-reclaimable, ACPI-reclaimable, ACPI NVS, reserved, bad-memory, executable/module, framebuffer, reserved-mapped, and unknown types remain unavailable.
- No memory is reclaimed based on assumed module or firmware lifetimes.

## Metadata and ownership
Two fixed-size bitmaps live in kernel BSS: one records frames explicitly reported as `USABLE`; the second records allocation ownership. Initialization starts fail-closed with every frame reserved and opens only validated usable frames. Each bitmap occupies 128 KiB for the 4 GiB / 4 KiB tracking window (256 KiB total).

## Interface
- `physical_allocator_initialize(map)` validates the map and initializes bitmap state.
- `physical_allocator_allocate(&address)` returns an aligned physical frame address or fails on exhaustion.
- `physical_allocator_free(address)` rejects frame zero, unaligned/out-of-range/reserved/reclaimable addresses, and double frees.
- `physical_allocator_get_stats(&stats)` reports managed, free, and tracked frame counts.

It returns physical addresses only. It does not dereference frames, create mappings, zero memory, or provide a virtual pointer. Callers must not access a returned frame until a future mapping policy explicitly allows it.

## Concurrency and limitations
The current kernel is a single-CPU bootstrap with interrupts disabled. This allocator has no lock and is not SMP-safe; do not call it concurrently or from interrupt handlers. It does not yet support contiguous multi-page allocations.

## Self-tests
The QEMU self-test kernel checks allocation and exhaustion, correct free/reallocation, rejection of reserved and reclaimable ranges, invalid/double-free operations, tracking-window clipping, and physical frame zero exclusion. BIOS and UEFI QEMU CI are required before validation is claimed.
