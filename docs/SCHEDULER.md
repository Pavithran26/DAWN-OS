# Single-CPU preemptive kernel-thread scheduler

## Status

The scheduler uses saved x86-64 interrupt frames for context switches. Voluntary yields use software vector 0x42; Local APIC timer vector 0x41 can preempt a running worker when the periodic timer is armed. The Local APIC timer is calibrated against a 50 ms PIT channel 2 reference and configured for a nominal 100 Hz scheduler tick (10 ms quantum). This is a scheduler reference interval, not a wall-clock clocksource.

## Thread model

- One bootstrap context plus a fixed pool of eight kernel threads.
- Each worker receives a private 16 KiB static stack; stack memory is not dynamically allocated.
- Each stack has a 64-byte canary region checked during scheduling and at test completion.
- Thread IDs are monotonic nonzero 64-bit values.
- Thread creation is restricted to the bootstrap context before worker execution begins; workers cannot dynamically create child threads in this increment.
- States are FREE, READY, RUNNING and DEAD. A DEAD slot can be reused after its stack guard is verified.
- No user-mode threads, process address spaces, priorities, blocking I/O, wait queues, mutexes, or SMP.

## Context switching

Every worker is initialized with a synthetic same-privilege interrupt frame at the top of its guarded 16 KiB stack. The common ISR epilogue restores general-purpose registers and uses IRETQ to enter the C bootstrap with SysV function-entry alignment. A real interrupt saves the current worker's frame; the dispatcher may return a different worker's saved frame, and the assembly epilogue restores that frame before IRETQ. When a thread entry returns, it is marked DEAD and never scheduled again.

The scheduler disables interrupts while changing thread state. Voluntary yields restore the caller's original IF state when that context resumes. New worker frames start with IF set so the periodic Local APIC timer can preempt them. When the timer is not armed, a worker that never yields can still monopolize the CPU.

## Scheduling

- Round-robin among READY worker threads on voluntary reschedule or Local APIC timer interrupts.
- Hardware timer dispatch requires exactly one RUNNING worker (the interrupted thread); software reschedule accepts only the documented READY/DEAD transition states and no worker may remain RUNNING at dispatch entry. Every saved worker frame must be 8-byte aligned and fully contained above its low-stack canary before it can be restored.
- scheduler_run_until_idle() runs READY workers from the bootstrap context and returns to bootstrap after all workers exit.
- kernel_thread_yield() makes the current worker READY and switches to another READY worker when one exists.
- kernel_thread_exit() marks the current worker DEAD and switches to another READY worker or the waiting bootstrap context.
- The scheduler is single-CPU and not safe for concurrent CPU execution.

## Validation

The QEMU self-test creates two worker threads. Each increments an independent counter four times while voluntarily yielding. It checks both counters, unique thread IDs, exactly two thread exits, zero active workers, the yield/switch counters, and intact stack guards. A second test runs four two-worker preemption rounds. In each round the first worker busy-waits without yielding; the second worker must run on a timer preemption, update shared progress and exit while the first worker is READY. An eight-worker barrier stress then starts eight non-yielding workers; none may finish until every worker has run, so the test requires at least seven timer preemptions. The tests also check repeated DEAD-slot reuse, stack guards, timer-preemption counts, saved-frame stack bounds, canary integrity, and scheduler switch/exit counts. APIC tests verify multiple periodic deliveries, that ticks stop after masking, and that a restarted timer delivers again.

BIOS and UEFI QEMU tests must retain all earlier ACPI/MADT, xAPIC/IOAPIC real PIT IRQ, page-table and physical allocator tests.

## Not implemented

Timer-frequency calibration and wall-clock accounting, scheduler quantum policy, sleep/block/wakeup, priorities, a dedicated idle thread, per-CPU state, SMP, locks, user threads and process scheduling.


## Additional lifecycle validation

The scheduler self-test also rejects a null entry point, fills all eight worker slots, rejects a ninth concurrent worker, checks unique IDs, drains the pool, and verifies that completed (DEAD) slots can be reused with monotonically increasing IDs. These tests exercise the fixed-capacity and reuse rules; they do not imply preemption or concurrent/SMP safety.
