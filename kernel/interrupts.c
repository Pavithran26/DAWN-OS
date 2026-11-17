/* SPDX-License-Identifier: Unlicense */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "interrupts.h"
#include "apic.h"
#include "scheduler.h"
#include "syscall.h"

#define IDT_ENTRIES 256U
#define IDT_EXCEPTION_COUNT 32U
#define PIC_MASTER_COMMAND 0x20U
#define PIC_MASTER_DATA 0x21U
#define PIC_SLAVE_COMMAND 0xa0U
#define PIC_SLAVE_DATA 0xa1U
#define PIC_MASTER_OFFSET 0x20U
#define PIC_SLAVE_OFFSET 0x28U
#define PIT_COMMAND 0x43U
#define PIT_CHANNEL0 0x40U
#define PIT_BASE_HZ 1193182U
#define PIT_TEST_HZ 1000U
#define QEMU_DEBUG_EXIT_PORT 0xf4U
#define QEMU_TEST_FAIL 0x11U
#define SERIAL_COM1 0x3f8U
#define SERIAL_POLL_LIMIT 100000U

#define IDT_INTERRUPT_GATE 0x8eU
#define IDT_USER_INTERRUPT_GATE 0xeeU
#define INTERRUPT_VECTOR_TIMER 32U
#define INTERRUPT_VECTOR_PIC_LAST 47U

struct idt_entry {
    uint16_t offset_low;
    uint16_t selector;
    uint8_t ist;
    uint8_t attributes;
    uint16_t offset_middle;
    uint32_t offset_high;
    uint32_t reserved;
} __attribute__((packed));

struct idtr {
    uint16_t limit;
    uint64_t base;
} __attribute__((packed));

_Static_assert(sizeof(struct idt_entry) == 16U, "x86_64 IDT gate must be 16 bytes");
_Static_assert(sizeof(struct idtr) == 10U, "x86_64 IDTR must be 10 bytes");
_Static_assert(offsetof(struct interrupt_frame, vector) == 120U,
               "ISR register save order does not match C frame");
_Static_assert(offsetof(struct interrupt_frame, rip) == 136U,
               "ISR error/vector frame layout mismatch");
_Static_assert(offsetof(struct interrupt_frame, user_rsp) == 160U,
               "privilege-transition stack fields must follow same-CPL frame");
_Static_assert(sizeof(struct interrupt_frame) == 176U,
               "maximum privilege-transition interrupt frame layout mismatch");

extern void isr_default_stub(void);
extern void isr_spurious_stub(void);
extern void isr_stub_64(void);
extern void isr_stub_65(void);
extern void isr_stub_66(void);
extern void isr_syscall_stub(void);
extern void (*isr_stub_table[48])(void);

static struct idt_entry idt[IDT_ENTRIES] __attribute__((aligned(16)));
static bool idt_loaded;
static uint16_t kernel_cs;
static volatile uint64_t timer_ticks;
static volatile uint64_t breakpoint_count;
static volatile uint64_t spurious_interrupt_count;
static volatile bool breakpoint_expected;

static inline void outb(uint16_t port, uint8_t value) {
    __asm__ volatile ("outb %0, %1" : : "a"(value), "Nd"(port));
}
static inline uint8_t inb(uint16_t port) {
    uint8_t value;
    __asm__ volatile ("inb %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}
static inline void io_wait(void) {
    outb(0x80U, 0U);
}
#ifdef DAWN_KERNEL_TEST
static inline void cpu_pause(void) {
    __asm__ volatile ("pause");
}
#endif
static inline void cpu_cli(void) {
    __asm__ volatile ("cli" : : : "memory");
}
static uint16_t read_cs(void) {
    uint16_t value;
    __asm__ volatile ("mov %%cs, %0" : "=r"(value));
    return value;
}
#ifdef DAWN_KERNEL_TEST
static void qemu_debug_exit(uint8_t code) {
    outb((uint16_t)QEMU_DEBUG_EXIT_PORT, code);
}
#endif

static void serial_write_char(char value) {
    for (uint32_t attempt=0U; attempt<SERIAL_POLL_LIMIT; ++attempt) {
        if ((inb((uint16_t)(SERIAL_COM1+5U)) & 0x20U)!=0U) {
            outb((uint16_t)SERIAL_COM1,(uint8_t)value);
            return;
        }
    }
}
static void serial_write(const char *text) {
    while (*text!='\0') {
        if (*text=='\n') serial_write_char('\r');
        serial_write_char(*text++);
    }
}
static void serial_write_hex(uint64_t value) {
    static const char digits[]="0123456789abcdef";
    bool started=false;
    for (int shift=60; shift>=0; shift-=4) {
        const uint8_t nibble=(uint8_t)((value>>(uint32_t)shift)&0xfULL);
        if (nibble!=0U || started || shift==0) {
            serial_write_char(digits[nibble]);
            started=true;
        }
    }
}

static void idt_set_gate_attributes(uint8_t vector, void (*handler)(void),
                                    uint8_t attributes) {
    const uint64_t address=(uint64_t)(uintptr_t)handler;
    struct idt_entry *entry=&idt[vector];
    entry->offset_low=(uint16_t)(address & 0xffffULL);
    entry->selector=kernel_cs;
    entry->ist=0U;
    entry->attributes=attributes;
    entry->offset_middle=(uint16_t)((address>>16)&0xffffULL);
    entry->offset_high=(uint32_t)(address>>32);
    entry->reserved=0U;
}

static void idt_set_gate(uint8_t vector, void (*handler)(void)) {
    idt_set_gate_attributes(vector,handler,IDT_INTERRUPT_GATE);
}

static void pic_write_masks(uint8_t master,uint8_t slave) {
    outb((uint16_t)PIC_MASTER_DATA,master);
    outb((uint16_t)PIC_SLAVE_DATA,slave);
}
static void pic_remap_and_mask(void) {
    outb((uint16_t)PIC_MASTER_DATA,0xffU);
    outb((uint16_t)PIC_SLAVE_DATA,0xffU);

    outb((uint16_t)PIC_MASTER_COMMAND,0x11U); io_wait();
    outb((uint16_t)PIC_SLAVE_COMMAND,0x11U); io_wait();
    outb((uint16_t)PIC_MASTER_DATA,(uint8_t)PIC_MASTER_OFFSET); io_wait();
    outb((uint16_t)PIC_SLAVE_DATA,(uint8_t)PIC_SLAVE_OFFSET); io_wait();
    outb((uint16_t)PIC_MASTER_DATA,0x04U); io_wait();
    outb((uint16_t)PIC_SLAVE_DATA,0x02U); io_wait();
    outb((uint16_t)PIC_MASTER_DATA,0x01U); io_wait();
    outb((uint16_t)PIC_SLAVE_DATA,0x01U); io_wait();

    /* All device interrupts remain masked until an explicit driver enables one. */
    pic_write_masks(0xffU,0xffU);
}
static void pic_send_eoi(uint8_t irq) {
    if (irq>=8U) outb((uint16_t)PIC_SLAVE_COMMAND,0x20U);
    outb((uint16_t)PIC_MASTER_COMMAND,0x20U);
}
#ifdef DAWN_KERNEL_TEST
static void pit_program_frequency(uint32_t frequency_hz) {
    uint32_t divisor=PIT_BASE_HZ/frequency_hz;
    if (divisor==0U) divisor=1U;
    if (divisor>65535U) divisor=65535U;
    outb((uint16_t)PIT_COMMAND,0x36U); /* channel 0, lobyte/hibyte, mode 3 */
    outb((uint16_t)PIT_CHANNEL0,(uint8_t)(divisor&0xffU));
    outb((uint16_t)PIT_CHANNEL0,(uint8_t)((divisor>>8)&0xffU));
}
static uint16_t pit_read_counter(void) {
    uint8_t low=0U, high=0U;
    outb((uint16_t)PIT_COMMAND,0x00U); /* latch channel 0 count */
    low=inb((uint16_t)PIT_CHANNEL0);
    high=inb((uint16_t)PIT_CHANNEL0);
    return (uint16_t)((uint16_t)low | ((uint16_t)high<<8));
}
#endif

static _Noreturn void interrupt_fatal(const struct interrupt_frame *frame) {
    cpu_cli();
    serial_write("\nDAWN-OS: FATAL: unhandled exception/interrupt vector=");
    serial_write_hex(frame->vector);
    serial_write(" error=");
    serial_write_hex(frame->error_code);
    serial_write(" rip=");
    serial_write_hex(frame->rip);
    if (frame->vector==14ULL) {
        uint64_t cr2=0ULL;
        __asm__ volatile ("mov %%cr2, %0" : "=r"(cr2));
        serial_write(" cr2=");
        serial_write_hex(cr2);
    }
    serial_write("\n");
#ifdef DAWN_KERNEL_TEST
    qemu_debug_exit(QEMU_TEST_FAIL);
#endif
    for (;;) {
        __asm__ volatile ("cli; hlt" : : : "memory");
    }
}

struct interrupt_frame *interrupt_dispatch(struct interrupt_frame *frame) {
    if (frame==NULL) {
        cpu_cli();
#ifdef DAWN_KERNEL_TEST
        qemu_debug_exit(QEMU_TEST_FAIL);
#endif
        for (;;) __asm__ volatile ("cli; hlt" : : : "memory");
    }

    if (frame->vector==(uint64_t)DAWN_SYSCALL_VECTOR) {
        frame->rax=(uint64_t)syscall_dispatch(
            frame->rax,frame->rdi,frame->rsi,frame->rdx,
            frame->r10,frame->r8,frame->r9);
        return frame;
    }
    if (frame->vector==255ULL) {
        /* APIC spurious interrupts are not in the in-service register: no EOI. */
        ++spurious_interrupt_count;
        return frame;
    }
    if (frame->vector==(uint64_t)APIC_IO_IRQ_VECTOR) {
        apic_handle_ioapic_interrupt();
        return frame;
    }
    if (frame->vector==(uint64_t)APIC_LOCAL_TIMER_VECTOR) {
        apic_handle_local_timer_interrupt();
        return scheduler_handle_interrupt(frame,true);
    }
    if (frame->vector==(uint64_t)SCHEDULER_RESCHEDULE_VECTOR) {
        return scheduler_handle_interrupt(frame,false);
    }
    if (frame->vector==3ULL && breakpoint_expected &&
        frame->cs==(uint64_t)kernel_cs &&
        (frame->rflags & 0x2ULL)!=0ULL) {
        ++breakpoint_count;
        return frame;
    }

    if (frame->vector<IDT_EXCEPTION_COUNT) {
        interrupt_fatal(frame);
    }
    if (frame->vector>=INTERRUPT_VECTOR_TIMER &&
        frame->vector<=INTERRUPT_VECTOR_PIC_LAST) {
        const uint8_t irq=(uint8_t)(frame->vector-INTERRUPT_VECTOR_TIMER);
        if (irq==0U) ++timer_ticks;
        pic_send_eoi(irq);
        return frame;
    }
    interrupt_fatal(frame);
}

bool interrupts_initialize(void) {
    struct idtr descriptor={0};
    if (idt_loaded) return false;

    kernel_cs=read_cs();
    if (kernel_cs==0U) return false;

    for (uint32_t vector=0U; vector<IDT_ENTRIES; ++vector) {
        idt_set_gate((uint8_t)vector,isr_default_stub);
    }
    for (uint32_t vector=0U; vector<48U; ++vector) {
        idt_set_gate((uint8_t)vector,isr_stub_table[vector]);
    }
    /* Limine leaves the local APIC spurious vector at 0xFF. */
    idt_set_gate(255U,isr_spurious_stub);
    idt_set_gate((uint8_t)APIC_IO_IRQ_VECTOR,isr_stub_64);
    idt_set_gate((uint8_t)APIC_LOCAL_TIMER_VECTOR,isr_stub_65);
    idt_set_gate((uint8_t)SCHEDULER_RESCHEDULE_VECTOR,isr_stub_66);
    /* DPL3 callers enter on the TSS.RSP0 stack. */
    idt_set_gate_attributes((uint8_t)DAWN_SYSCALL_VECTOR,isr_syscall_stub,
                            IDT_USER_INTERRUPT_GATE);

    descriptor.limit=(uint16_t)(sizeof(idt)-1U);
    descriptor.base=(uint64_t)(uintptr_t)&idt[0];
    __asm__ volatile ("lidt %0" : : "m"(descriptor) : "memory");
    idt_loaded=true;

    pic_remap_and_mask();
    timer_ticks=0ULL;
    breakpoint_count=0ULL;
    spurious_interrupt_count=0ULL;
    breakpoint_expected=false;
    return true;
}

#ifdef DAWN_KERNEL_TEST
bool interrupts_self_test(void) {
    uint64_t initial_ticks=0ULL;
    uint16_t initial_pit_count=0U;
    bool pit_observed=false;

    if (!idt_loaded ||
        inb((uint16_t)PIC_MASTER_DATA)!=0xffU ||
        inb((uint16_t)PIC_SLAVE_DATA)!=0xffU) {
        return false;
    }

    /* INT3 must traverse the real IDT with a valid same-privilege frame. */
    breakpoint_expected=true;
    __asm__ volatile ("int3" : : : "memory");
    breakpoint_expected=false;
    if (breakpoint_count!=1ULL) return false;

    /* CPL0 may execute the DPL3 gate too; exercise the real IDT dispatch. */
    {
        uint64_t result=UINT64_MAX;
        __asm__ volatile ("xor %%eax, %%eax; int $0x80"
                          : "=a"(result) : : "rcx","r11","memory","cc");
        if (result!=(uint64_t)DAWN_SYSCALL_ABI_VERSION ||
            idt[DAWN_SYSCALL_VECTOR].attributes!=IDT_USER_INTERRUPT_GATE)
            return false;
    }

    /* APIC vector 0xFF is spurious and must return without sending an EOI. */
    __asm__ volatile ("int $0xff" : : : "memory");
    if (spurious_interrupt_count!=1ULL) return false;

    /* Verify that the PIT hardware counter advances without enabling IRQs. */
    pit_program_frequency(PIT_TEST_HZ);
    initial_pit_count=pit_read_counter();
    for (uint64_t attempt=0ULL; attempt<1000000ULL; ++attempt) {
        if (pit_read_counter()!=initial_pit_count) {
            pit_observed=true;
            break;
        }
        cpu_pause();
    }
    if (!pit_observed) return false;

    /* Exercise the installed IRQ0 vector and EOI path with a controlled
     * software interrupt. External IRQ delivery is deferred until APIC routing. */
    initial_ticks=timer_ticks;
    __asm__ volatile ("int $0x20" : : : "memory");
    return timer_ticks==initial_ticks+1ULL;
}
#endif
