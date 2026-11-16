# ACPI MADT and xAPIC/IOAPIC bring-up

## Firmware handoff

DAWN-OS requests the Limine RSDP response. Under the requested Base Revision 6, Limine response pointers are HHDM virtual pointers; the RSDP's XSDT/RSDT and root-table entry addresses are physical addresses and are converted using the runtime HHDM offset. ACPI tables are guaranteed by Base Revision 4+ to reside in HHDM-mapped ACPI reclaimable, ACPI NVS, or reserved-mapped regions.

The parser checks RSDP signature and checksums, validates the extended RSDP checksum/length when present, chooses XSDT when available (RSDT fallback), verifies root-table signature/length/checksum, bounds the root entry count, and scans for the APIC (MADT) table. The MADT parser validates the table checksum and every variable-length entry before advancing. It currently supports one IOAPIC, the BSP's legacy Local APIC processor entry, the IOAPIC base/GSI base, ISA IRQ0 source override, and Local APIC address override. Unknown entries are skipped by their validated lengths; unsupported multi-IOAPIC or x2APIC configurations are rejected safely.

## MMIO mappings

The page-table manager offers a separate one-page MMIO mapper. It finds an unused high-half virtual page rather than assuming a fixed HHDM address, and sets PWT+PCD to select Limine's specified x86-64 PAT[3] uncached type. MMIO physical pages do not pass through the ordinary physical RAM allocator. The returned MMIO mapping is not freed by the RAM allocator.

## xAPIC and IOAPIC

The APIC initializer:
- Requires CPUID APIC support and the xAPIC (not x2APIC) mode.
- Requires IA32_APIC_BASE enabled and consistent with the MADT Local APIC address.
- Maps LAPIC and IOAPIC registers as uncached MMIO.
- Checks the current LAPIC ID against the BSP APIC ID parsed from the MADT.
- Enables the existing Local APIC spurious vector and sets task priority to zero.
- Reads IOAPIC version/GSI range and masks ordinary fixed/lowest-priority redirection entries while preserving special firmware delivery modes.

During the bounded PIT test only, the MADT-resolved ISA IRQ0 GSI is programmed as a fixed physical-destination route to vector 0x40 and briefly unmasked. The 8259 PIC remains fully masked. The vector handler increments a monotonic count and sends EOI to the LAPIC. The route is masked and IF is cleared before the test returns.

## Local APIC timer

The BSP Local APIC timer has a dedicated IDT vector (0x41), separate from the IOAPIC PIT route (0x40). Initialization leaves the timer masked with an initial count of zero. The timer handler increments a monotonic tick counter and acknowledges the Local APIC with EOI. The driver exposes explicit one-shot and periodic start calls plus a stop call; each start configures divide-by-16 and accepts a raw hardware count. No timer frequency is claimed: calibration against a reference clock is a separate requirement before selecting a scheduler quantum. The QEMU self-test arms a bounded one-shot and verifies delivery before masking and stopping it again. A separate scheduler self-test arms the periodic timer and verifies that a busy-waiting worker is preempted so a peer worker can run. The scheduler's software reschedule vector (0x42) does not send a LAPIC EOI.

## Deliberate limits

- Single BSP CPU only; no AP startup, x2APIC, MSI/MSI-X, interrupt remapping, SMP routing, or per-CPU EOI.
- One IOAPIC is supported in this increment. Systems with multiple IOAPICs are rejected instead of guessed.
- Only the ISA PIT IRQ0 route is enabled in the test path. Other IOAPIC inputs remain masked.
- No general device-driver IRQ registration API yet.
- Local timer counts are not calibrated to nanoseconds or hertz; no scheduler quantum is derived from them yet.
- The periodic Local APIC timer can preempt a single-CPU kernel thread through saved interrupt frames and the scheduler's round-robin dispatcher. Its raw counter is not yet calibrated to wall-clock time.
- Physical hardware validation is not claimed; current verification is QEMU BIOS and OVMF UEFI.

## QEMU verification

The self-test first validates the MADT parser against a synthetic valid table and a checksum-corrupted table. It then parses the actual firmware MADT, initializes xAPIC/IOAPIC, routes PIT IRQ0 using the ACPI source override, and verifies a real hardware interrupt reached IDT vector 0x40 under both BIOS and UEFI.
