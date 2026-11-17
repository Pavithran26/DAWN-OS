/* SPDX-License-Identifier: Unlicense */
#ifndef DAWN_APIC_H
#define DAWN_APIC_H

#include <stdbool.h>
#include <stdint.h>

#include "acpi.h"

#define APIC_IO_IRQ_VECTOR 0x40U
#define APIC_LOCAL_TIMER_VECTOR 0x41U

bool apic_initialize(const struct acpi_platform_info *platform);
bool apic_route_pit_irq_test(uint32_t global_irq, uint16_t flags);
void apic_handle_ioapic_interrupt(void);
void apic_handle_local_timer_interrupt(void);
uint64_t apic_delivered_irq_count(void);
uint64_t apic_local_timer_tick_count(void);
bool apic_local_timer_start_oneshot(uint32_t initial_count);
bool apic_local_timer_start_periodic(uint32_t initial_count);
bool apic_local_timer_calibrate(uint32_t target_hz);
bool apic_local_timer_start_calibrated_periodic(void);
uint32_t apic_local_timer_calibrated_hz(void);
uint32_t apic_local_timer_calibrated_period_count(void);
void apic_local_timer_stop(void);
#ifdef DAWN_KERNEL_TEST
bool apic_local_timer_self_test(void);
bool apic_local_timer_periodic_self_test(void);
#endif
bool apic_is_initialized(void);

#endif
