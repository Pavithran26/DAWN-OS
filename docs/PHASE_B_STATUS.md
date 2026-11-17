# Phase B — Kernel foundations status

## Gate

**In progress. Do not start Phase C runtime implementation until the Phase B validation gate is met.**

The code now includes a single-CPU kernel-thread scheduler that can switch saved interrupt frames and preempt a running worker when the Local APIC periodic timer is armed. This is an implementation status, not a claim that the BIOS/UEFI test matrix has passed.

## Implemented foundations

- Limine x86_64 boot and framebuffer handoff validation.
- Memory-map validation and HHDM response validation.
- Physical 4 KiB page allocator with reserved-region protection and allocator self-tests.
- Four-level page-table manager, mapping permissions, software translation and MMIO mapping.
- x86_64 IDT and exception dispatch, PIC masking, ACPI MADT discovery, xAPIC and IOAPIC setup.
- IOAPIC-routed PIT interrupt delivery test.
- Local APIC timer vector 0x41, one-shot and periodic controls, EOI handling, PIT channel 2 reference calibration, nominal 100 Hz scheduler-tick configuration and periodic stop/restart self-tests.
- Saved interrupt-frame switching, kernel-only software reschedule vector 0x42, cooperative yield, thread exit, explicit dispatch-state invariants, per-thread saved-frame bounds validation, and four two-worker preemption rounds, plus an eight-worker non-yielding barrier stress test with timer-preemption accounting.
- Fixed-capacity worker pool, stack guards, monotonic thread IDs and DEAD-slot reuse checks.

## Remaining Phase B work

1. Review calibration accuracy, PIT channel 2 mode-0 behavior and timer-count bounds on both QEMU firmware paths; add measurement tolerance checks.
2. Add longer-running scheduler stress coverage, including timer preemption while workers are near exit and repeated stop/restart under load.
3. Complete interrupt-safe diagnostics and shared-state critical-section review.
4. Run and pass the complete QEMU BIOS and OVMF UEFI build/boot/self-test matrix after the code changes.

## Explicitly out of scope for this Phase B gate

- SMP and application processor startup.
- Ring 3 processes, syscall ABI, ELF loading and userspace.
- VirtIO storage, filesystem and networking.
- Desktop renderer, terminal, file manager, partition manager and task manager.
- Physical hardware and Secure Boot validation.

## Completion rule

Phase B is complete only after the remaining timer/scheduler work is implemented, the full BIOS and UEFI tests pass, and the results are reviewed. Until then Phase C remains a planning document.
