# Single-CPU cooperative kernel-thread scheduler

## Status

The first scheduler increment is cooperative. It does not preempt running code from the timer interrupt. The PIT/IOAPIC hardware IRQ path is independently validated; switching an arbitrary interrupt frame is a later, separately tested change.

## Thread model

- One bootstrap context plus a fixed pool of eight kernel threads.
- Each worker receives a private 16 KiB static stack; stack memory is not dynamically allocated.
- Each stack has a 64-byte canary region checked during scheduling and at test completion.
- Thread IDs are monotonic nonzero 64-bit values.
- Thread creation is restricted to the bootstrap context before worker execution begins; workers cannot dynamically create child threads in this increment.
- States are FREE, READY, RUNNING and DEAD. A DEAD slot can be reused after its stack guard is verified.
- No user-mode threads, process address spaces, priorities, blocking I/O, wait queues, mutexes, or SMP.

## Context switching

The x86_64 assembly routine saves/restores the SysV callee-saved registers (RBX, RBP, R12–R15) and RSP. New stacks are built so the first return enters an assembly trampoline with a 16-byte aligned stack; the trampoline calls the C bootstrap using the SysV ABI. When a thread entry returns, it is marked DEAD and never scheduled again.

The scheduler disables interrupts while changing thread state and selecting the next context. Cooperative yields restore the caller's original IF state when that context resumes. A newly created worker starts with interrupts disabled. Kernel thread entries must call yield; a thread that never yields monopolizes the CPU.

## Scheduling

- Round-robin among READY worker threads.
- scheduler_run_until_idle() runs READY workers from the bootstrap context and returns to bootstrap after all workers exit.
- kernel_thread_yield() makes the current worker READY and switches to another READY worker when one exists.
- kernel_thread_exit() marks the current worker DEAD and switches to another READY worker or the waiting bootstrap context.
- The scheduler is single-CPU and not safe for concurrent CPU execution.

## Validation

The QEMU self-test creates two worker threads. Each increments an independent counter four times while voluntarily yielding. It checks both counters, unique thread IDs, exactly two thread exits, zero active workers, the yield/switch counters, and intact stack guards.

BIOS and UEFI QEMU tests must retain all earlier ACPI/MADT, xAPIC/IOAPIC real PIT IRQ, page-table and physical allocator tests.

## Not implemented

Timer-driven preemption, interrupt-frame context switching, scheduler quantum/accounting, sleep/block/wakeup, priorities, idle halt loop, per-CPU state, SMP, locks, user threads and process scheduling.


## Additional lifecycle validation

The scheduler self-test also rejects a null entry point, fills all eight worker slots, rejects a ninth concurrent worker, checks unique IDs, drains the pool, and verifies that completed (DEAD) slots can be reused with monotonically increasing IDs. These tests exercise the fixed-capacity and reuse rules; they do not imply preemption or concurrent/SMP safety.
