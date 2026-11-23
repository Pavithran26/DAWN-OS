# Phase B — Kernel foundations status

## Gate

**COMPLETED / PASSED. Phase B validation gate is officially met.**

The single-CPU kernel-thread scheduler, Local APIC timer calibration correctness, interrupt safety, stack guard integrity, and scheduler preemption stress suites have been thoroughly hardened, deterministically validated, and verified across both BIOS and UEFI QEMU environments with 0 failures (exit code 33).

## Implemented and Verified Foundations

- **Boot & Firmware:** Limine x86_64 boot and framebuffer handoff validation across SeaBIOS and OVMF UEFI firmware.
- **Memory Management:**
  - Physical 4 KiB frame allocator (PMM) with bitmap tracking, reserved-region protection, allocation ownership, double-free rejection, and allocator self-tests.
  - Four-level page-table manager (VMM), page protection permissions, software address translation, and MMIO mapping.
- **Interrupts & Controllers:**
  - x86_64 IDT with 256 gates, CPU exception dispatch, legacy PIC masking, ACPI MADT table discovery, xAPIC MMIO mapping, and IOAPIC routing.
  - Verified IOAPIC-routed PIT interrupt delivery.
- **Timer Calibration Correctness:**
  - PIT channel 2 mode-0 50 ms reference measurement.
  - Bounded period counts with explicit `[LAPIC_TIMER_MIN_PERIOD_COUNT (500), LAPIC_TIMER_MAX_PERIOD_COUNT (10,000,000)]` enforcement to catch stuck timers or out-of-spec clock dividers.
  - Target frequency bound verification rejecting out-of-range rates (`0 Hz`, `< 10 Hz`, `> 1000 Hz`) and NULL pointer outputs.
  - Deterministic 2% repeatability tolerance test (`apic_local_timer_calibration_tolerance_test`) ensuring consecutive measurements agree within tight bounds.
  - Nominal 100 Hz calibrated periodic timer lifecycle with clean start and stop primitives.
- **Scheduler & Threading Foundations:**
  - Single-CPU cooperative and preemptive round-robin thread scheduler.
  - Saved interrupt-frame context switching via dedicated reschedule vector `0x42`.
  - Thread state machine transitions (`THREAD_FREE` -> `THREAD_READY` -> `THREAD_RUNNING` -> `THREAD_DEAD`).
  - Thread stack bounds with 64-byte canary guard bytes (`0xa5U`), detecting stack corruption and rejecting reuse of damaged thread slots (`scheduler_guard_and_state_self_test`).
  - Safe slot reuse, monotonic thread ID assignment, and comprehensive cleanup helper (`scheduler_cleanup_all_threads`).
- **Scheduler Stress Testing:**
  - Deterministic multi-worker preemption stress suite (`scheduler_stress_self_test`) featuring 4 non-yielding worker threads arranged in a dependency barrier ring.
  - Physical verification that progress requires Local APIC timer preemption interrupts, with guaranteed `timer_preemptions >= before + 3` accounting.
  - Preemption near worker exit validation.
  - Repeated timer stop/start churn during active thread scheduling (8 cycles of timer shutdown and restart across yielding peer threads).
  - Clean error recovery and post-cleanup thread dispatch validation.
- **Production Smoke Boot:** Automated BIOS and UEFI smoke testing verifying production ISO boots directly into the desktop event loop with zero kernel panics or fatal errors.

## Test Verification Summary

- **Total Self-Tests:** 67 tests executed in sequence.
- **QEMU BIOS Boot:** PASS (Exit code 33, 67/67 tests passed, 0 FAIL).
- **QEMU UEFI Boot:** PASS (Exit code 33, 67/67 tests passed, 0 FAIL).
- **Production Smoke Test:** PASS (Both BIOS and UEFI reach production desktop loop).
- **Compiler Flags:** Strict compliance with `-Wall -Wextra -Werror -Wconversion -Wshadow -Wundef`.

## Remaining Scope / Phase C Handoff

With the completion of Phase B kernel foundations, development moves forward to **Phase C — Userspace and Storage Integration**, building on the verified GDT/TSS, ring 3 privilege boundaries, process lifecycle, virtual file system, and VirtIO drivers.
