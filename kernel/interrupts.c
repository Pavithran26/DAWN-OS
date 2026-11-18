/* SPDX-License-Identifier: Unlicense */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "interrupts.h"
#include "gdt.h"
#include "apic.h"
#include "scheduler.h"
#include "syscall.h"
#include "page_table.h"
#include "process.h"
#include "physical_memory.h"

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

#define USER_CANONICAL_LIMIT (1ULL << 47)
#define RFLAGS_FIXED_BIT (1ULL << 1)
#define RFLAGS_IOPL_MASK (3ULL << 12)
#define RFLAGS_NT (1ULL << 14)
#define RFLAGS_VM (1ULL << 17)

bool interrupt_frame_from_user(const struct interrupt_frame *frame) {
    return frame != NULL && (frame->cs & 3ULL) == 3ULL;
}

bool interrupt_frame_user_structure_valid(
    const struct interrupt_frame *frame
) {
    if (!interrupt_frame_from_user(frame)) return false;
    /* These fields are present only for a privilege-changing CPL3 trap. */
    if (frame->cs!=(uint64_t)DAWN_USER_CODE_SELECTOR+3ULL ||
        frame->user_ss!=(uint64_t)DAWN_USER_DATA_SELECTOR+3ULL)
        return false;
    return (frame->rflags&RFLAGS_FIXED_BIT)!=0ULL &&
           (frame->rflags&(RFLAGS_IOPL_MASK|RFLAGS_NT|RFLAGS_VM))==0ULL;
}

bool interrupt_frame_user_context_valid(const struct interrupt_frame *frame) {
    uint64_t physical=0ULL;
    uint32_t code_flags=0U,stack_flags=0U;
    if (!interrupt_frame_user_structure_valid(frame) ||
        frame->rip==0ULL || frame->rip>=USER_CANONICAL_LIMIT ||
        frame->user_rsp==0ULL || frame->user_rsp>=USER_CANONICAL_LIMIT)
        return false;

    /* Validate the actual return instruction and writable user stack mapping. */
    if (!vm_translate(frame->rip,&physical,&code_flags) ||
        (code_flags&(uint32_t)VM_PAGE_USER)==0U ||
        (code_flags&(uint32_t)VM_PAGE_EXECUTE)==0U ||
        (code_flags&(uint32_t)VM_PAGE_NO_EXECUTE)!=0U)
        return false;
    if (!vm_translate(frame->user_rsp-1ULL,&physical,&stack_flags) ||
        (stack_flags&(uint32_t)VM_PAGE_USER)==0U ||
        (stack_flags&(uint32_t)VM_PAGE_WRITE)==0U)
        return false;
    return true;
}

#ifdef DAWN_KERNEL_TEST
static bool interrupt_find_and_map_user_test_page(uint64_t physical,
                                                   uint32_t flags,
                                                   uint64_t *virtual_address) {
    uint64_t translated=0ULL;
    if (virtual_address==NULL) return false;
    for (uint64_t pml4=1ULL;pml4<256ULL;pml4++) {
        for (uint64_t pdpt=0ULL;pdpt<512ULL;pdpt++) {
            const uint64_t candidate=(pml4<<39)|(pdpt<<30)|0x23456000ULL;
            if (candidate>=USER_CANONICAL_LIMIT) continue;
            if (vm_translate(candidate,&translated,NULL)) continue;
            if (vm_map_page(candidate,physical,flags)) {
                *virtual_address=candidate;
                return true;
            }
        }
    }
    return false;
}

static bool interrupt_user_frame_mapping_self_test(void) {
    uint64_t code_physical=0ULL,stack_physical=0ULL;
    uint64_t code_virtual=0ULL,stack_virtual=0ULL;
    bool code_mapped=false,stack_mapped=false,result=false;
    struct interrupt_frame frame={0};
    if (!physical_allocator_allocate(&code_physical)) return false;
    if (!physical_allocator_allocate(&stack_physical)) {
        (void)physical_allocator_free(code_physical);
        return false;
    }
    code_mapped=interrupt_find_and_map_user_test_page(
        code_physical,(uint32_t)VM_PAGE_USER|(uint32_t)VM_PAGE_EXECUTE,
        &code_virtual);
    if (code_mapped) {
        stack_mapped=interrupt_find_and_map_user_test_page(
            stack_physical,(uint32_t)VM_PAGE_USER|(uint32_t)VM_PAGE_WRITE,
            &stack_virtual);
    }
    if (code_mapped && stack_mapped) {
        frame.cs=(uint64_t)DAWN_USER_CODE_SELECTOR+3ULL;
        frame.user_ss=(uint64_t)DAWN_USER_DATA_SELECTOR+3ULL;
        frame.rip=code_virtual+16ULL;
        frame.user_rsp=stack_virtual+4096ULL;
        frame.rflags=0x202ULL;
        result=interrupt_frame_user_context_valid(&frame);
        frame.rip=stack_virtual+16ULL;
        if (interrupt_frame_user_context_valid(&frame)) result=false;
        frame.rip=code_virtual+16ULL;
        frame.user_rsp=code_virtual+4096ULL;
        if (interrupt_frame_user_context_valid(&frame)) result=false;
    }
    if (stack_mapped && !vm_unmap_page(stack_virtual)) result=false;
    if (code_mapped && !vm_unmap_page(code_virtual)) result=false;
    if (!physical_allocator_free(stack_physical)) result=false;
    if (!physical_allocator_free(code_physical)) result=false;
    return result;
}
#endif

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

    if ((frame->cs&3ULL)==3ULL) {
        if (process_active_id()==0ULL) interrupt_fatal(frame);

        if (frame->vector==(uint64_t)DAWN_SYSCALL_VECTOR) {
            if (!interrupt_frame_user_structure_valid(frame))
                interrupt_fatal(frame);
            if (frame->rax==(uint64_t)DAWN_SYS_EXIT ||
                !interrupt_frame_user_context_valid(frame))
                return scheduler_user_exit(frame);
            frame->rax=(uint64_t)syscall_dispatch(
                frame->rax,frame->rdi,frame->rsi,frame->rdx,
                frame->r10,frame->r8,frame->r9);
            return frame;
        }
        if (frame->vector==(uint64_t)APIC_LOCAL_TIMER_VECTOR ||
            frame->vector==(uint64_t)APIC_IO_IRQ_VECTOR ||
            frame->vector==255ULL) {
            if (!interrupt_frame_user_structure_valid(frame))
                interrupt_fatal(frame);
            if (frame->vector==(uint64_t)APIC_LOCAL_TIMER_VECTOR) {
                /* Acknowledge the LAPIC even when a bad user return frame is
                 * about to terminate the process. */
                apic_handle_local_timer_interrupt();
                if (!interrupt_frame_user_context_valid(frame))
                    return scheduler_user_exit(frame);
                return scheduler_handle_interrupt(frame,true);
            }
            if (frame->vector==(uint64_t)APIC_IO_IRQ_VECTOR) {
                apic_handle_ioapic_interrupt();
                if (!interrupt_frame_user_context_valid(frame))
                    return scheduler_user_exit(frame);
                return frame;
            }
            ++spurious_interrupt_count;
            if (!interrupt_frame_user_context_valid(frame))
                return scheduler_user_exit(frame);
            return frame;
        }
        /* Recoverable user exceptions terminate only the faulting process.
         * NMI, double fault and machine check remain kernel-fatal. */
        if (frame->vector<IDT_EXCEPTION_COUNT &&
            frame->vector!=2ULL && frame->vector!=8ULL &&
            frame->vector!=18ULL &&
            interrupt_frame_user_structure_valid(frame))
            return scheduler_user_exit(frame);
        interrupt_fatal(frame);
    } else if ((frame->cs&3ULL)!=0ULL) {
        interrupt_fatal(frame);
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

    /* Structural privilege-frame rejection plus mapped user RIP/RSP checks. */
    {
        struct interrupt_frame frame={0};
        frame.cs=(uint64_t)kernel_cs;
        if (interrupt_frame_from_user(&frame) ||
            interrupt_frame_user_context_valid(&frame))
            return false;
        frame.cs=(uint64_t)DAWN_USER_CODE_SELECTOR+3ULL;
        frame.user_ss=(uint64_t)DAWN_USER_DATA_SELECTOR+3ULL;
        frame.rip=0x400000ULL;
        frame.user_rsp=0x7ffffff000ULL;
        frame.rflags=0x202ULL;
        if (!interrupt_frame_from_user(&frame) ||
            interrupt_frame_user_context_valid(&frame))
            return false;
        frame.user_ss=(uint64_t)DAWN_KERNEL_DATA_SELECTOR;
        if (interrupt_frame_user_context_valid(&frame)) return false;
        frame.user_ss=(uint64_t)DAWN_USER_DATA_SELECTOR+3ULL;
        frame.rflags|=RFLAGS_IOPL_MASK;
        if (interrupt_frame_user_context_valid(&frame)) return false;
        frame.rflags=0x202ULL;
        frame.rip=USER_CANONICAL_LIMIT;
        if (interrupt_frame_user_context_valid(&frame)) return false;
    }
    if (!interrupt_user_frame_mapping_self_test()) return false;

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

    /*
     * Exercise the CPL3 syscall dispatch policy with a synthetic frame backed
     * by a real temporary process address space. This is not an IRETQ launch.
     */
    {
        const uint64_t code_va=0x0000004000000000ULL;
        const uint64_t stack_va=0x0000004000001000ULL;
        uint64_t process_id=0ULL,code_page=0ULL,stack_page=0ULL;
        struct process_info info={0};
        struct vm_address_space space={0};
        struct interrupt_frame frame={0};
        bool active=false,created=false,code_allocated=false;
        bool stack_allocated=false,cleaned=false,result=false;
        if (!physical_allocator_allocate(&code_page)) goto syscall_user_cleanup;
        code_allocated=true;
        if (!physical_allocator_allocate(&stack_page)) goto syscall_user_cleanup;
        stack_allocated=true;
        if (!process_create(&process_id)) goto syscall_user_cleanup;
        created=true;
        if (!process_mark_ready(process_id) ||
            !process_get_info(process_id,&info)) goto syscall_user_cleanup;
        space.root_physical=info.address_space_root;
        if (!vm_address_space_map_user(&space,code_va,code_page,
                (uint32_t)VM_PAGE_USER|(uint32_t)VM_PAGE_EXECUTE) ||
            !vm_address_space_map_user(&space,stack_va,stack_page,
                (uint32_t)VM_PAGE_USER|(uint32_t)VM_PAGE_WRITE) ||
            !process_activate(process_id)) goto syscall_user_cleanup;
        active=true;
        frame.vector=(uint64_t)DAWN_SYSCALL_VECTOR;
        frame.cs=(uint64_t)DAWN_USER_CODE_SELECTOR+3ULL;
        frame.user_ss=(uint64_t)DAWN_USER_DATA_SELECTOR+3ULL;
        frame.rip=code_va+16ULL;
        frame.user_rsp=stack_va+4096ULL;
        frame.rflags=0x202ULL;
        frame.rax=DAWN_SYS_ABI_VERSION;
        result=interrupt_dispatch(&frame)==&frame &&
               frame.rax==(uint64_t)DAWN_SYSCALL_ABI_VERSION;
syscall_user_cleanup:
        if (active) {
            if (process_deactivate()) active=false;
            else result=false;
        }
        if (created && !active) {
            if (process_mark_exited(process_id) &&
                process_reap(process_id)) cleaned=true;
            else result=false;
        }
        if ((!created || cleaned) && stack_allocated &&
            !physical_allocator_free(stack_page)) result=false;
        if ((!created || cleaned) && code_allocated &&
            !physical_allocator_free(code_page)) result=false;
        if (!result || active || (created && !cleaned)) return false;
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
