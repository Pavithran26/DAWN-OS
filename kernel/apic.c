/* SPDX-License-Identifier: Unlicense */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "acpi.h"
#include "apic.h"
#include "page_table.h"

#define IA32_APIC_BASE_MSR 0x1bU
#define IA32_APIC_BASE_ENABLE (1ULL << 11)
#define IA32_APIC_BASE_X2APIC (1ULL << 10)
#define IA32_APIC_BASE_ADDRESS_MASK 0x000ffffffffff000ULL
#define CPUID_APIC_FEATURE (1U << 9)

#define LAPIC_ID 0x020U
#define LAPIC_TPR 0x080U
#define LAPIC_EOI 0x0b0U
#define LAPIC_SVR 0x0f0U
#define LAPIC_LVT_TIMER 0x320U
#define LAPIC_TIMER_INITIAL_COUNT 0x380U
#define LAPIC_TIMER_CURRENT_COUNT 0x390U
#define LAPIC_TIMER_DIVIDE_CONFIG 0x3e0U

#define LAPIC_SVR_VECTOR_MASK 0xffU
#define LAPIC_SVR_SOFTWARE_ENABLE (1U << 8)
#define LAPIC_LVT_MASK (1U << 16)
#define LAPIC_LVT_PERIODIC (1U << 17)
#define LAPIC_TIMER_DIVIDE_BY_16 0x3U

#define IOAPIC_REG_ID 0x00U
#define IOAPIC_REG_VERSION 0x01U
#define IOAPIC_REG_REDIR_BASE 0x10U
#define IOAPIC_REDIR_MASK (1U << 16)
#define IOAPIC_REDIR_POLARITY_LOW (1U << 13)
#define IOAPIC_REDIR_TRIGGER_LEVEL (1U << 15)

#define PIT_COMMAND 0x43U
#define PIT_CHANNEL0 0x40U
#define PIT_CHANNEL2 0x42U
#define PIT_SPEAKER_CONTROL 0x61U
#define PIT_CHANNEL2_MODE0_LOHI 0xb0U
#define PIT_BASE_HZ 1193182U
#define PIT_TEST_HZ 1000U
#define PIT_CALIBRATION_INTERVAL_MS 50U
#define PIT_CALIBRATION_COUNT ((PIT_BASE_HZ*PIT_CALIBRATION_INTERVAL_MS)/1000U)
#define LAPIC_TIMER_CALIBRATION_MAX_POLLS 100000000ULL
#define LAPIC_TIMER_MIN_PERIOD_COUNT 500U
#define LAPIC_TIMER_MAX_PERIOD_COUNT 10000000U

static volatile uint32_t *local_apic;
static volatile uint32_t *io_apic;
static uint32_t io_apic_gsi_base;
static uint32_t io_apic_redirection_count;
static uint32_t bsp_apic_id;
static bool initialized;
static volatile uint64_t delivered_irq_count;
static volatile uint64_t local_timer_tick_count;
static uint32_t local_timer_calibrated_hz;
static uint32_t local_timer_calibrated_period_count;

static void cpu_cpuid(uint32_t leaf, uint32_t subleaf,
                      uint32_t *eax, uint32_t *ebx,
                      uint32_t *ecx, uint32_t *edx) {
    __asm__ volatile ("cpuid"
                      : "=a"(*eax), "=b"(*ebx), "=c"(*ecx), "=d"(*edx)
                      : "a"(leaf), "c"(subleaf));
}

static bool cpu_has_apic(void) {
    uint32_t eax=1U, ebx=0U, ecx=0U, edx=0U;
    cpu_cpuid(1U,0U,&eax,&ebx,&ecx,&edx);
    return (edx & CPUID_APIC_FEATURE)!=0U;
}

static uint64_t read_apic_base_msr(void) {
    uint32_t low=0U, high=0U;
    __asm__ volatile ("rdmsr"
                      : "=a"(low), "=d"(high)
                      : "c"(IA32_APIC_BASE_MSR));
    return ((uint64_t)high<<32)|(uint64_t)low;
}

static uint32_t local_apic_read(uint32_t offset) {
    __asm__ volatile ("lfence" : : : "memory");
    return local_apic[offset/sizeof(uint32_t)];
}

static void local_apic_write(uint32_t offset,uint32_t value) {
    local_apic[offset/sizeof(uint32_t)]=value;
    __asm__ volatile ("mfence" : : : "memory");
}

static uint32_t io_apic_read(uint8_t reg) {
    io_apic[0]=reg;
    __asm__ volatile ("mfence" : : : "memory");
    const uint32_t value=io_apic[4];
    __asm__ volatile ("lfence" : : : "memory");
    return value;
}

static void io_apic_write(uint8_t reg,uint32_t value) {
    io_apic[0]=reg;
    __asm__ volatile ("mfence" : : : "memory");
    io_apic[4]=value;
    __asm__ volatile ("mfence" : : : "memory");
}

static void io_apic_mask_all(void) {
    for (uint32_t index=0U;index<io_apic_redirection_count;index++) {
        const uint32_t low_reg=IOAPIC_REG_REDIR_BASE+(index*2U);
        const uint32_t low=io_apic_read((uint8_t)low_reg);
        const uint32_t delivery_mode=(low>>8)&0x7U;
        /* Only mask fixed/lowest entries; preserve firmware-owned SMI/other modes. */
        if (delivery_mode==0U || delivery_mode==1U) {
            io_apic_write((uint8_t)low_reg,low|IOAPIC_REDIR_MASK);
        }
    }
}

static bool io_apic_route_irq(uint32_t global_irq,uint16_t flags,
                              uint8_t vector,uint8_t destination_apic_id,
                              bool masked) {
    uint32_t redirection_index=0U;
    uint32_t low_reg=0U;
    uint32_t low=0U;
    uint32_t high=0U;
    const uint16_t polarity=flags&0x3U;
    const uint16_t trigger=(flags>>2)&0x3U;

    if (io_apic==NULL || local_apic==NULL) return false;
    if (global_irq<io_apic_gsi_base ||
        (polarity==2U) || (trigger==2U)) return false;

    redirection_index=global_irq-io_apic_gsi_base;
    if (redirection_index>=io_apic_redirection_count) return false;
    low_reg=IOAPIC_REG_REDIR_BASE+(redirection_index*2U);
    {
        const uint32_t existing_low=io_apic_read((uint8_t)low_reg);
        const uint32_t existing_delivery_mode=(existing_low>>8)&0x7U;
        /* Never commandeer a firmware-owned NMI/SMI/ExtINT or reserved route. */
        if (existing_delivery_mode!=0U && existing_delivery_mode!=1U) {
            return false;
        }
    }

    low=(uint32_t)vector;
    if (polarity==3U) low|=IOAPIC_REDIR_POLARITY_LOW;
    if (trigger==3U) low|=IOAPIC_REDIR_TRIGGER_LEVEL;
    if (masked) low|=IOAPIC_REDIR_MASK;

    high=(uint32_t)destination_apic_id<<24;
    /* Write the destination first and keep the entry masked until complete. */
    io_apic_write((uint8_t)(low_reg+1U),high);
    io_apic_write((uint8_t)low_reg,low|IOAPIC_REDIR_MASK);
    if (!masked) {
        io_apic_write((uint8_t)low_reg,low&~IOAPIC_REDIR_MASK);
    }
    return true;
}

static void pit_program_frequency(uint32_t frequency_hz) {
    uint32_t divisor=PIT_BASE_HZ/frequency_hz;
    if (divisor==0U) divisor=1U;
    if (divisor>65535U) divisor=65535U;
    __asm__ volatile ("outb %0, %1" : : "a"((uint8_t)0x36U),
                      "Nd"((uint16_t)PIT_COMMAND));
    __asm__ volatile ("outb %0, %1" : :
                      "a"((uint8_t)(divisor&0xffU)),
                      "Nd"((uint16_t)PIT_CHANNEL0));
    __asm__ volatile ("outb %0, %1" : :
                      "a"((uint8_t)((divisor>>8)&0xffU)),
                      "Nd"((uint16_t)PIT_CHANNEL0));
}

static uint64_t cpu_read_rflags(void) {
    uint64_t flags=0ULL;
    __asm__ volatile ("pushfq; popq %0" : "=r"(flags));
    return flags;
}
static void cpu_restore_interrupt_flag(uint64_t flags) {
    if ((flags&(1ULL<<9))!=0ULL)
        __asm__ volatile ("sti" : : : "memory");
}
static inline uint8_t inb(uint16_t port) {
    uint8_t value=0U;
    __asm__ volatile ("inb %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}
static void cpu_sti(void) {
    __asm__ volatile ("sti" : : : "memory");
}
static void cpu_cli(void) {
    __asm__ volatile ("cli" : : : "memory");
}
static void cpu_pause(void) {
    __asm__ volatile ("pause");
}

bool apic_initialize(const struct acpi_platform_info *platform) {
    uint64_t apic_base_msr=0ULL;
    uint64_t msr_physical_base=0ULL;
    uint64_t local_apic_virtual=0ULL;
    uint64_t io_apic_virtual=0ULL;
    uint32_t local_id=0U;
    uint32_t io_version=0U;
    uint32_t io_id=0U;

    if (initialized || platform==NULL || !platform->bsp_local_apic_found ||
        !platform->io_apic_found || !cpu_has_apic()) {
        return false;
    }

    apic_base_msr=read_apic_base_msr();
    if ((apic_base_msr&IA32_APIC_BASE_X2APIC)!=0ULL ||
        (apic_base_msr&IA32_APIC_BASE_ENABLE)==0ULL) {
        /* x2APIC MSR access is not implemented in this xAPIC MMIO increment. */
        return false;
    }
    msr_physical_base=apic_base_msr&IA32_APIC_BASE_ADDRESS_MASK;
    if (msr_physical_base!=platform->local_apic_physical_address) {
        return false;
    }

    if (!vm_map_mmio_page(platform->local_apic_physical_address,
                          &local_apic_virtual) ||
        !vm_map_mmio_page((uint64_t)platform->io_apic_physical_address,
                          &io_apic_virtual)) {
        return false;
    }

    local_apic=(volatile uint32_t *)(uintptr_t)local_apic_virtual;
    io_apic=(volatile uint32_t *)(uintptr_t)io_apic_virtual;

    local_id=(local_apic_read(LAPIC_ID)>>24)&0xffU;
    bsp_apic_id=platform->bsp_apic_id;
    if (local_id!=bsp_apic_id) return false;

    io_id=io_apic_read(IOAPIC_REG_ID);
    (void)io_id;
    io_version=io_apic_read(IOAPIC_REG_VERSION);
    io_apic_redirection_count=((io_version>>16)&0xffU)+1U;
    io_apic_gsi_base=platform->io_apic_gsi_base;
    if (io_apic_redirection_count==0U ||
        io_apic_redirection_count>120U ||
        platform->pit_irq_gsi<io_apic_gsi_base ||
        (platform->pit_irq_gsi-io_apic_gsi_base)>=io_apic_redirection_count) {
        return false;
    }

    /* Keep the BSP's priority open and enable the configured spurious vector. */
    local_apic_write(LAPIC_TPR,0U);
    /* Keep the local timer inert until a caller explicitly arms it. */
    local_apic_write(LAPIC_LVT_TIMER,
                     APIC_LOCAL_TIMER_VECTOR|LAPIC_LVT_MASK);
    local_apic_write(LAPIC_TIMER_INITIAL_COUNT,0U);
    local_apic_write(LAPIC_TIMER_DIVIDE_CONFIG,LAPIC_TIMER_DIVIDE_BY_16);
    local_apic_write(LAPIC_SVR,
        (local_apic_read(LAPIC_SVR)&~LAPIC_SVR_VECTOR_MASK) |
        LAPIC_SVR_SOFTWARE_ENABLE | 0xffU);

    io_apic_mask_all();
    initialized=true;
    delivered_irq_count=0ULL;
    local_timer_tick_count=0ULL;
    local_timer_calibrated_hz=0U;
    local_timer_calibrated_period_count=0U;
    return true;
}

void apic_handle_ioapic_interrupt(void) {
    if (!initialized) return;
    ++delivered_irq_count;
    local_apic_write(LAPIC_EOI,0U);
}

void apic_handle_local_timer_interrupt(void) {
    if (!initialized) return;
    ++local_timer_tick_count;
    local_apic_write(LAPIC_EOI,0U);
}

uint64_t apic_delivered_irq_count(void) {
    return delivered_irq_count;
}

uint64_t apic_local_timer_tick_count(void) {
    return local_timer_tick_count;
}

static bool apic_local_timer_start(uint32_t initial_count,bool periodic) {
    uint32_t lvt=APIC_LOCAL_TIMER_VECTOR;
    if (!initialized || local_apic==NULL || initial_count==0U) return false;
    local_apic_write(LAPIC_LVT_TIMER,lvt|LAPIC_LVT_MASK);
    local_apic_write(LAPIC_TIMER_INITIAL_COUNT,0U);
    local_apic_write(LAPIC_TIMER_DIVIDE_CONFIG,LAPIC_TIMER_DIVIDE_BY_16);
    if (periodic) lvt|=LAPIC_LVT_PERIODIC;
    local_apic_write(LAPIC_LVT_TIMER,lvt);
    local_apic_write(LAPIC_TIMER_INITIAL_COUNT,initial_count);
    return true;
}

bool apic_local_timer_calibrate_measure(uint32_t target_hz, uint32_t *out_period_count) {
    const uint64_t saved_flags=cpu_read_rflags();
    uint8_t speaker_control=0U;
    uint32_t start_count=0U;
    uint32_t end_count=0U;
    uint32_t elapsed_count=0U;
    uint32_t period_count=0U;
    bool output_started=false;
    bool reference_elapsed=false;
    bool success=false;

    if (!initialized || local_apic==NULL || out_period_count==NULL ||
        target_hz<10U || target_hz>1000U)
        return false;

    cpu_cli();
    speaker_control=inb((uint16_t)PIT_SPEAKER_CONTROL);
    apic_local_timer_stop();

    /* PIT channel 2 supplies a 50 ms mode-0 reference. Channel 0 is kept
     * for the IOAPIC IRQ test; speaker output remains disabled. */
    __asm__ volatile ("outb %0, %1" : : "a"((uint8_t)(speaker_control&~0x03U)),
                      "Nd"((uint16_t)PIT_SPEAKER_CONTROL));
    __asm__ volatile ("outb %0, %1" : : "a"((uint8_t)PIT_CHANNEL2_MODE0_LOHI),
                      "Nd"((uint16_t)PIT_COMMAND));
    __asm__ volatile ("outb %0, %1" : : "a"((uint8_t)(PIT_CALIBRATION_COUNT&0xffU)),
                      "Nd"((uint16_t)PIT_CHANNEL2));
    __asm__ volatile ("outb %0, %1" : :
                      "a"((uint8_t)((PIT_CALIBRATION_COUNT>>8)&0xffU)),
                      "Nd"((uint16_t)PIT_CHANNEL2));

    /* Mode 0 drives OUT2 low until the programmed count expires. */
    if ((inb((uint16_t)PIT_SPEAKER_CONTROL)&0x20U)==0U) {
        local_apic_write(LAPIC_LVT_TIMER,
                         APIC_LOCAL_TIMER_VECTOR|LAPIC_LVT_MASK);
        local_apic_write(LAPIC_TIMER_DIVIDE_CONFIG,LAPIC_TIMER_DIVIDE_BY_16);
        local_apic_write(LAPIC_TIMER_INITIAL_COUNT,UINT32_MAX);
        /* Rising GATE2 starts the PIT one-shot. */
        __asm__ volatile ("outb %0, %1" : :
                          "a"((uint8_t)((speaker_control&~0x02U)|0x01U)),
                          "Nd"((uint16_t)PIT_SPEAKER_CONTROL));
        start_count=local_apic_read(LAPIC_TIMER_CURRENT_COUNT);
        output_started=true;
    }

    if (output_started) {
        for (uint64_t attempt=0ULL;attempt<LAPIC_TIMER_CALIBRATION_MAX_POLLS;attempt++) {
            if ((inb((uint16_t)PIT_SPEAKER_CONTROL)&0x20U)!=0U) {
                reference_elapsed=true;
                break;
            }
            cpu_pause();
        }
    }

    if (reference_elapsed) {
        end_count=local_apic_read(LAPIC_TIMER_CURRENT_COUNT);
        if (end_count<start_count) {
            elapsed_count=start_count-end_count;
            if (elapsed_count>0U) {
                const uint64_t numerator=(uint64_t)elapsed_count*1000ULL;
                const uint64_t denominator=(uint64_t)PIT_CALIBRATION_INTERVAL_MS*
                                            (uint64_t)target_hz;
                const uint64_t calculated=numerator/denominator;
                if (calculated>=LAPIC_TIMER_MIN_PERIOD_COUNT &&
                    calculated<=LAPIC_TIMER_MAX_PERIOD_COUNT) {
                    period_count=(uint32_t)calculated;
                    success=true;
                }
            }
        }
    }

    apic_local_timer_stop();
    __asm__ volatile ("outb %0, %1" : : "a"(speaker_control),
                      "Nd"((uint16_t)PIT_SPEAKER_CONTROL));
    if (success) {
        *out_period_count=period_count;
    }
    cpu_restore_interrupt_flag(saved_flags);
    return success;
}

bool apic_local_timer_calibrate(uint32_t target_hz) {
    uint32_t period_count=0U;
    local_timer_calibrated_hz=0U;
    local_timer_calibrated_period_count=0U;
    if (!apic_local_timer_calibrate_measure(target_hz,&period_count)) {
        return false;
    }
    local_timer_calibrated_hz=target_hz;
    local_timer_calibrated_period_count=period_count;
    return true;
}

bool apic_local_timer_start_calibrated_periodic(void) {
    if (local_timer_calibrated_hz==0U ||
        local_timer_calibrated_period_count==0U) return false;
    return apic_local_timer_start_periodic(local_timer_calibrated_period_count);
}

uint32_t apic_local_timer_calibrated_hz(void) {
    return local_timer_calibrated_hz;
}

uint32_t apic_local_timer_calibrated_period_count(void) {
    return local_timer_calibrated_period_count;
}

bool apic_local_timer_start_oneshot(uint32_t initial_count) {
    return apic_local_timer_start(initial_count,false);
}

bool apic_local_timer_start_periodic(uint32_t initial_count) {
    return apic_local_timer_start(initial_count,true);
}

void apic_local_timer_stop(void) {
    if (!initialized || local_apic==NULL) return;
    local_apic_write(LAPIC_LVT_TIMER,
                     APIC_LOCAL_TIMER_VECTOR|LAPIC_LVT_MASK);
    local_apic_write(LAPIC_TIMER_INITIAL_COUNT,0U);
}

#ifdef DAWN_KERNEL_TEST
bool apic_local_timer_self_test(void) {
    uint64_t initial_ticks=0ULL;
    bool delivered=false;
    if (!initialized || local_apic==NULL) return false;
    initial_ticks=apic_local_timer_tick_count();
    if (!apic_local_timer_start_oneshot(1000000U)) return false;
    cpu_sti();
    for (uint64_t attempt=0ULL;attempt<100000000ULL;attempt++) {
        if (apic_local_timer_tick_count()>initial_ticks) {
            delivered=true;
            break;
        }
        cpu_pause();
    }
    cpu_cli();
    apic_local_timer_stop();
    return delivered;
}
bool apic_local_timer_periodic_self_test(void) {
    uint64_t initial_ticks=0ULL;
    uint64_t stopped_ticks=0ULL;
    bool delivered=false;
    if (local_timer_calibrated_hz==0U ||
        local_timer_calibrated_period_count==0U) return false;

    if (!apic_local_timer_start_calibrated_periodic()) return false;
    initial_ticks=apic_local_timer_tick_count();
    cpu_sti();
    for (uint64_t attempt=0ULL;attempt<LAPIC_TIMER_CALIBRATION_MAX_POLLS;attempt++) {
        if (apic_local_timer_tick_count()>=initial_ticks+3ULL) {
            delivered=true;
            break;
        }
        cpu_pause();
    }
    cpu_cli();
    apic_local_timer_stop();
    apic_local_timer_stop();
    if (!delivered) return false;

    stopped_ticks=apic_local_timer_tick_count();
    for (uint64_t attempt=0ULL;attempt<10000000ULL;attempt++) cpu_pause();
    if (apic_local_timer_tick_count()!=stopped_ticks) return false;

    if (!apic_local_timer_start_calibrated_periodic()) return false;
    initial_ticks=apic_local_timer_tick_count();
    cpu_sti();
    delivered=false;
    for (uint64_t attempt=0ULL;attempt<LAPIC_TIMER_CALIBRATION_MAX_POLLS;attempt++) {
        if (apic_local_timer_tick_count()>initial_ticks) {
            delivered=true;
            break;
        }
        cpu_pause();
    }
    cpu_cli();
    apic_local_timer_stop();
    return delivered;
}

bool apic_local_timer_api_self_test(void) {
    const uint32_t calibrated_hz=local_timer_calibrated_hz;
    const uint32_t calibrated_count=local_timer_calibrated_period_count;
    if (!initialized || calibrated_hz==0U || calibrated_count==0U) return false;
    if (apic_local_timer_start_oneshot(0U) ||
        apic_local_timer_start_periodic(0U) ||
        apic_local_timer_calibrate(0U) ||
        apic_local_timer_calibrate(9U) ||
        apic_local_timer_calibrate(1001U)) return false;
    return local_timer_calibrated_hz==calibrated_hz &&
           local_timer_calibrated_period_count==calibrated_count;
}

bool apic_local_timer_calibration_tolerance_test(void) {
    uint32_t first_count=0U;
    uint32_t second_count=0U;
    uint32_t diff=0U;
    uint64_t max_allowed_diff=0ULL;

    if (!initialized || local_apic==NULL) return false;
    if (local_timer_calibrated_hz!=100U) return false;
    first_count=local_timer_calibrated_period_count;
    if (first_count<LAPIC_TIMER_MIN_PERIOD_COUNT ||
        first_count>LAPIC_TIMER_MAX_PERIOD_COUNT) return false;

    /* Perform a second independent measurement at 100 Hz */
    if (!apic_local_timer_calibrate_measure(100U,&second_count)) return false;
    if (second_count<LAPIC_TIMER_MIN_PERIOD_COUNT ||
        second_count>LAPIC_TIMER_MAX_PERIOD_COUNT) return false;

    /* Repeatability tolerance: measurements must agree within 2% */
    diff=(first_count>second_count) ? (first_count-second_count) : (second_count-first_count);
    max_allowed_diff=((uint64_t)first_count*2ULL)/100ULL;
    if (max_allowed_diff==0ULL) max_allowed_diff=1ULL;
    if ((uint64_t)diff>max_allowed_diff) return false;

    /* Verify bounds and invalid argument rejection */
    if (apic_local_timer_calibrate_measure(0U,&second_count)) return false;
    if (apic_local_timer_calibrate_measure(9U,&second_count)) return false;
    if (apic_local_timer_calibrate_measure(1001U,&second_count)) return false;
    if (apic_local_timer_calibrate_measure(100U,NULL)) return false;

    /* Verify state preservation */
    return (local_timer_calibrated_hz==100U &&
            local_timer_calibrated_period_count==first_count);
}
#endif

bool apic_is_initialized(void) {
    return initialized;
}

bool apic_route_pit_irq_test(uint32_t global_irq,uint16_t flags) {
    uint64_t initial_count=0ULL;
    bool delivered=false;

    if (!initialized ||
        !io_apic_route_irq(global_irq,flags,APIC_IO_IRQ_VECTOR,
                           (uint8_t)bsp_apic_id,true)) {
        return false;
    }

    pit_program_frequency(PIT_TEST_HZ);
    initial_count=apic_delivered_irq_count();
    if (!io_apic_route_irq(global_irq,flags,APIC_IO_IRQ_VECTOR,
                           (uint8_t)bsp_apic_id,false)) {
        return false;
    }

    cpu_sti();
    for (uint64_t attempt=0ULL;attempt<100000000ULL;attempt++) {
        if (apic_delivered_irq_count()>initial_count) {
            delivered=true;
            break;
        }
        cpu_pause();
    }
    cpu_cli();
    (void)io_apic_route_irq(global_irq,flags,APIC_IO_IRQ_VECTOR,
                            (uint8_t)bsp_apic_id,true);

    return delivered;
}
