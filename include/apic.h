/* SPDX-License-Identifier: Unlicense */
#ifndef DAWN_APIC_H
#define DAWN_APIC_H

#include <stdbool.h>
#include <stdint.h>

#include "acpi.h"

#define APIC_IO_IRQ_VECTOR 0x40U

bool apic_initialize(const struct acpi_platform_info *platform);
bool apic_route_pit_irq_test(uint32_t global_irq, uint16_t flags);
void apic_handle_ioapic_interrupt(void);
uint64_t apic_delivered_irq_count(void);
bool apic_is_initialized(void);

#endif
