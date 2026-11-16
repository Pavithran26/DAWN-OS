# x86_64 exceptions, IDT, legacy PIC and PIT timer

## Scope and handoff assumptions

This is the initial single-CPU interrupt foundation. Limine base revision 6 enters x86_64 with IF clear, an undefined IDT, a bootloader-provided GDT, and legacy PIC IRQs masked. The IDT is installed before any interrupt is enabled. The current GDT code selector is read from CS and used in the IDT gates; the kernel does not replace or reclaim Limine's GDT.

The 64-bit IDT uses 16-byte interrupt gates. The assembly stubs normalize the stack to a vector and error-code pair, save all general-purpose registers, clear DF before calling C, and return with IRETQ. The dispatcher returns the frame to restore; this may be the same frame or a different thread's saved frame. The C interrupt frame includes the registers saved by the stubs, followed by the CPU-pushed RIP, CS and RFLAGS. For a same-privilege interrupt, the CPU does not push RSP/SS; those two fields are present only when the interrupt changes privilege level. The dispatcher therefore never reads them from a same-privilege frame.

## Exceptions

Vectors 0–31 have dedicated stubs. Vectors with architectural error-code pushes use the correct stub shape (8, 10–14, 17, 21, 29 and 30); other exception stubs synthesize a zero error-code slot. Unhandled exceptions log vector, error code, and RIP over COM1; page faults also report CR2. Fatal exceptions halt with interrupts disabled. A breakpoint is recoverable only while the explicit self-test flag is active.

## IDT

- 256 16-byte IDT gate entries, aligned to 16 bytes.
- Vectors 0–31 map to CPU exception stubs.
- Vectors 32–47 map to legacy PIC IRQ stubs; vector 0x40 is reserved for the tested IOAPIC-routed PIT interrupt.
- Vector 0x41 maps to the Local APIC timer stub; vector 0x42 is a kernel-only software reschedule vector; vector 0xFF maps to the APIC spurious stub.
- Vectors 48–63 and 67–254 route to a fatal default stub; vector 0x40 handles the IOAPIC PIT source, vector 0x41 handles the Local APIC timer, vector 0x42 performs a software reschedule without APIC EOI, and vector 255 uses the Local APIC spurious handler.
- Gate selector is the active kernel CS, DPL 0, interrupt-gate type, IST 0.
- Interrupts remain disabled during initialization.

## Interrupt controller and timer

This increment remaps the dual 8259 PIC to vectors 32–47 and leaves all 16 IRQ lines masked. EOI is sent to the slave PIC first for slave IRQs, then to the master. The 8254 PIT channel 0 is programmed in mode 3 and its latched hardware counter is polled to verify that it advances. The IRQ0 legacy IDT path is separately exercised with a controlled software INT 0x20. This validates the stub and PIC EOI dispatch but does not emulate a physical IRQ arrival; all legacy PIC lines remain masked.

Limine base revision 5+ provides a well-defined enabled xAPIC and masks fixed/lowest-priority I/O APIC redirection entries. The kernel now discovers RSDP/XSDT or RSDT and parses the MADT, maps the LAPIC/IOAPIC pages uncached, verifies the BSP APIC ID against MADT and IA32_APIC_BASE, masks fixed/lowest-priority IOAPIC entries (Limine already masks NMI/ExtINT entries), and routes only the MADT-resolved ISA PIT IRQ0 GSI to vector 0x40 during the bounded test. The legacy PIC remains fully masked. The test verifies a real PIT-triggered IOAPIC interrupt under QEMU BIOS and UEFI.

## Tests

The QEMU self-test executes INT3 through the real IDT, checks the APIC spurious vector, and validates the MADT parser using synthetic well-formed and checksum-corrupted tables. After discovering the real MADT, it programs PIT channel 0, unmasks only the IOAPIC GSI corresponding to ISA IRQ0, briefly enables interrupts with a bounded wait, verifies an actual vector-0x40 interrupt arrived, clears IF, and masks the route again. The legacy PIC stays masked throughout. The self-test also arms a one-shot Local APIC timer on vector 0x41 and checks that its handler increments the timer tick count and sends EOI. The periodic timer API currently accepts raw APIC counts; calibration and timer-driven scheduling remain future Phase B work. BIOS and UEFI runs must pass these tests.
