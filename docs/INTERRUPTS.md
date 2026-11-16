# x86_64 exceptions, IDT, legacy PIC and PIT timer

## Scope and handoff assumptions

This is the initial single-CPU interrupt foundation. Limine base revision 6 enters x86_64 with IF clear, an undefined IDT, a bootloader-provided GDT, and legacy PIC IRQs masked. The IDT is installed before any interrupt is enabled. The current GDT code selector is read from CS and used in the IDT gates; the kernel does not replace or reclaim Limine's GDT.

The 64-bit IDT uses 16-byte interrupt gates. The assembly stubs normalize the stack to a vector and error-code pair, save all general-purpose registers, clear DF before calling C, preserve the interrupted frame, and return with IRETQ. The saved frame includes RIP, CS, RFLAGS, RSP and SS: in 64-bit mode the processor pushes SS:RSP unconditionally.

## Exceptions

Vectors 0–31 have dedicated stubs. Vectors with architectural error-code pushes use the correct stub shape (8, 10–14, 17, 21, 29 and 30); other exception stubs synthesize a zero error-code slot. Unhandled exceptions log vector, error code, and RIP over COM1; page faults also report CR2. Fatal exceptions halt with interrupts disabled. A breakpoint is recoverable only while the explicit self-test flag is active.

## IDT

- 256 16-byte IDT gate entries, aligned to 16 bytes.
- Vectors 0–31 map to CPU exception stubs.
- Vectors 32–47 map to legacy PIC IRQ stubs.
- Remaining vectors route to a fatal default stub.
- Gate selector is the active kernel CS, DPL 0, interrupt-gate type, IST 0.
- Interrupts remain disabled during initialization.

## Interrupt controller and timer

This increment remaps the dual 8259 PIC to vectors 32–47 and leaves all 16 IRQ lines masked. EOI is sent to the slave PIC first for slave IRQs, then to the master. The 8254 PIT channel 0 is programmed in mode 3 and its latched hardware counter is polled to verify that it advances. The IRQ0 IDT path is separately exercised with a controlled software INT 0x20, which verifies the stub, dispatch and PIC EOI command path but does not emulate a physical IRQ arrival. No hardware IRQ line is unmasked and IF remains clear.

This is intentionally a legacy PIC/PIT bootstrap foundation, not yet a validated hardware interrupt-routing implementation. Limine hands off with the Local APIC enabled and its LINT routes masked; Local APIC/IOAPIC routing, ACPI MADT parsing, HPET, APIC timer, interrupt affinity, SMP, and device-driver IRQ registration are later work. No external IRQ is enabled in either the normal or test kernel path yet.

## Tests

The QEMU self-test executes INT3 through the real IDT and verifies the saved CS/SS/RSP frame. It then programs the PIT, unmasks only IRQ0, briefly enables interrupts with a bounded wait, verifies the timer tick counter advanced, disables interrupts, and masks IRQ0 again. BIOS and UEFI runs must pass this test.
