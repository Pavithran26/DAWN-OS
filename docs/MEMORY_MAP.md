# Boot memory-map intake and validation

## Status

Implemented as the first Phase B increment on the `bootstrap/phase-b-memory-map` branch.

The kernel requests Limine's memory-map feature at request revision 0. At boot, it validates the returned map and emits a small summary over COM1 serial. The QEMU self-test build also runs synthetic validator cases before validating the actual bootloader response.

**No physical allocator or virtual-memory manager is implemented by this change.** No memory is allocated, freed, reclaimed, zeroed, or written based on the memory map.

## Limine data model

The response contains:
- a response revision;
- an entry count;
- a pointer to an array of pointers to memory-map entries.

Each entry contains a physical base address, a byte length, and a type. The known type values in the current protocol are usable, reserved, ACPI reclaimable, ACPI NVS, bad memory, bootloader reclaimable, executable/modules, framebuffer, and reserved-mapped.

The exact identifiers and structure layouts are kept in `include/limine_requests.h`. They are the minimal declarations required by DAWN-OS and carry the upstream protocol header's 0BSD notice.

## Validation rules

The parser currently accepts at most 1,024 entries. It rejects:
- a missing response or output object;
- an empty map or a count above the configured limit;
- a null entry-pointer array or null entry pointer;
- zero-length regions;
- base-plus-length arithmetic overflow;
- entries not ordered by ascending base address;
- unaligned usable or bootloader-reclaimable regions;
- overlaps involving usable or bootloader-reclaimable regions;
- overflow in the per-type byte counters.

The protocol guarantees that entries are sorted by base address. It also guarantees 4 KiB alignment and non-overlap for usable and bootloader-reclaimable regions. Other, non-allocatable firmware regions are not guaranteed to be aligned or disjoint, so overlaps between those regions are accepted.

Unknown future entry types are counted and reported as unknown. They are not treated as usable RAM and are never candidates for allocation.

The validator checks null pointers and structure contents, but it cannot prove that an arbitrary pointer supplied by a bootloader is mapped and readable before dereferencing it. The implementation relies on the Limine handoff contract for the response and entry-array storage being accessible.

## Serial summary

The kernel reports:
- total memory-map entry count;
- usable region count and usable bytes in whole MiB;
- bootloader-reclaimable region count and bytes in whole MiB;
- count of entries with unknown future types.

Byte counts for non-allocatable types must not be summed into a total-memory figure: the protocol permits those regions to overlap.

## Self-tests

The `DAWN_KERNEL_TEST` build exercises:
- a valid sorted map;
- excessive entry count;
- missing pointer array and null entry pointer;
- zero-length range;
- base-plus-length overflow;
- unsorted entries;
- misaligned usable memory;
- overlap between usable and reserved memory;
- valid overlaps between non-allocatable firmware reservations;
- unknown type handling.

After these synthetic cases pass, the kernel validates the actual Limine map, requires at least one usable region, and reports the summary. The existing GitHub Actions job then boots the ISO under QEMU BIOS and UEFI.

## HHDM validation

The kernel now requests Limine's Higher Half Direct Map response. It requires a response, checks page alignment of the returned offset, and checks that adding the highest usable physical end does not overflow a 64-bit address. The offset is discovered at runtime and is never hard-coded. This is validation only: no arbitrary physical address is dereferenced through the HHDM yet.

## Not yet implemented

This code does not:
- initialize a frame allocator;
- reclaim bootloader-reclaimable memory;
- release ACPI regions;
- create or modify page tables;
- establish kernel heap allocation;
- manage memory ownership or lifetime.

Those operations require additional architecture and lifetime rules and must be implemented in later Phase B increments.
