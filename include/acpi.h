/* SPDX-License-Identifier: Unlicense */
#ifndef DAWN_ACPI_H
#define DAWN_ACPI_H

#include <stdbool.h>
#include <stdint.h>

#include "limine_requests.h"

struct acpi_platform_info {
    uint64_t local_apic_physical_address;
    uint32_t local_apic_flags;
    uint32_t bsp_apic_id;
    uint32_t io_apic_physical_address;
    uint32_t io_apic_gsi_base;
    uint32_t pit_irq_gsi;
    uint16_t pit_irq_flags;
    bool pcat_compatible;
    bool bsp_local_apic_found;
    bool io_apic_found;
};

bool acpi_discover_platform(
    uint64_t hhdm_offset,
    const struct limine_rsdp_response *rsdp_response,
    struct acpi_platform_info *platform
);

#ifdef DAWN_KERNEL_TEST
bool acpi_parser_self_test(void);
#endif

#endif
