/* SPDX-License-Identifier: Unlicense */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "acpi.h"

#define ACPI_RSDP_V1_SIZE 20U
#define ACPI_RSDP_V2_MIN_SIZE 36U
#define ACPI_RSDP_MAX_SIZE 4096U
#define ACPI_MAX_TABLE_SIZE (1024U * 1024U)
#define ACPI_MAX_ROOT_ENTRIES 256U
#define ACPI_MAX_MADT_ENTRIES 256U
#define ACPI_MADT_TYPE_LOCAL_APIC 0U
#define ACPI_MADT_TYPE_IO_APIC 1U
#define ACPI_MADT_TYPE_INTERRUPT_OVERRIDE 2U
#define ACPI_MADT_TYPE_LOCAL_APIC_ADDRESS_OVERRIDE 5U
#define ACPI_MADT_FLAG_PCAT_COMPAT 1U
#define ACPI_CPU_ENABLED 1U
#define ACPI_CPU_ONLINE_CAPABLE 2U

struct acpi_rsdp {
    char signature[8];
    uint8_t checksum;
    char oem_id[6];
    uint8_t revision;
    uint32_t rsdt_address;
    uint32_t length;
    uint64_t xsdt_address;
    uint8_t extended_checksum;
    uint8_t reserved[3];
} __attribute__((packed));

struct acpi_sdt_header {
    char signature[4];
    uint32_t length;
    uint8_t revision;
    uint8_t checksum;
    char oem_id[6];
    char oem_table_id[8];
    uint32_t oem_revision;
    uint32_t creator_id;
    uint32_t creator_revision;
} __attribute__((packed));

struct acpi_madt_header {
    struct acpi_sdt_header header;
    uint32_t local_apic_address;
    uint32_t flags;
} __attribute__((packed));

struct acpi_madt_entry_header {
    uint8_t type;
    uint8_t length;
} __attribute__((packed));

struct acpi_madt_local_apic {
    struct acpi_madt_entry_header header;
    uint8_t processor_uid;
    uint8_t apic_id;
    uint32_t flags;
} __attribute__((packed));

struct acpi_madt_io_apic {
    struct acpi_madt_entry_header header;
    uint8_t io_apic_id;
    uint8_t reserved;
    uint32_t address;
    uint32_t global_irq_base;
} __attribute__((packed));

struct acpi_madt_interrupt_override {
    struct acpi_madt_entry_header header;
    uint8_t bus;
    uint8_t source_irq;
    uint32_t global_irq;
    uint16_t flags;
} __attribute__((packed));

struct acpi_madt_local_apic_override {
    struct acpi_madt_entry_header header;
    uint16_t reserved;
    uint64_t address;
} __attribute__((packed));

_Static_assert(sizeof(struct acpi_rsdp) == 36U, "ACPI RSDP v2 size mismatch");
_Static_assert(sizeof(struct acpi_sdt_header) == 36U, "ACPI SDT header size mismatch");
_Static_assert(sizeof(struct acpi_madt_local_apic) == 8U, "MADT local APIC entry size mismatch");
_Static_assert(sizeof(struct acpi_madt_io_apic) == 12U, "MADT IO APIC entry size mismatch");
_Static_assert(sizeof(struct acpi_madt_interrupt_override) == 10U,
               "MADT interrupt override entry size mismatch");
_Static_assert(sizeof(struct acpi_madt_local_apic_override) == 12U,
               "MADT LAPIC address override size mismatch");

static bool is_canonical_address(uint64_t address) {
    const uint64_t upper = address >> 48;
    const bool sign = (address & (1ULL << 47)) != 0ULL;
    return sign ? upper == 0xffffULL : upper == 0ULL;
}

static bool add_address_checked(uint64_t base, uint64_t offset,
                                uint64_t *result) {
    if (result == NULL || offset > UINT64_MAX - base) {
        return false;
    }
    *result = base + offset;
    return is_canonical_address(*result);
}

static const void *physical_to_hhdm(uint64_t physical, uint64_t hhdm_offset) {
    uint64_t virtual_address = 0ULL;
    if (physical == 0ULL ||
        !add_address_checked(hhdm_offset, physical, &virtual_address)) {
        return NULL;
    }
    return (const void *)(uintptr_t)virtual_address;
}

static bool checksum_valid(const uint8_t *bytes, size_t length) {
    uint8_t sum = 0U;
    if (bytes == NULL || length == 0U) {
        return false;
    }
    for (size_t i = 0U; i < length; ++i) {
        sum = (uint8_t)(sum + bytes[i]);
    }
    return sum == 0U;
}

static bool signature_equals(const char *actual, const char expected[4]) {
    return actual != NULL &&
           actual[0] == expected[0] && actual[1] == expected[1] &&
           actual[2] == expected[2] && actual[3] == expected[3];
}

static uint32_t read_bsp_apic_id(void) {
    uint32_t eax=1U, ebx=0U, ecx=0U, edx=0U;
    __asm__ volatile ("cpuid"
                      : "+a"(eax), "=b"(ebx), "+c"(ecx), "=d"(edx));
    return (ebx>>24)&0xffU;
}

/* Pure parser: the input range is already mapped and bounded by the SDT header. */
static bool parse_madt(const void *table_bytes, size_t available,
                       uint32_t bsp_apic_id,
                       struct acpi_platform_info *platform) {
    const struct acpi_madt_header *madt =
        (const struct acpi_madt_header *)table_bytes;
    const uint8_t *cursor = NULL;
    const uint8_t *end = NULL;
    bool have_local_apic_address = false;
    uint32_t io_apic_count = 0U;
    uint32_t entry_count = 0U;
    struct acpi_platform_info parsed = {0};

    if (table_bytes == NULL || platform == NULL ||
        available < sizeof(struct acpi_madt_header) ||
        !signature_equals(madt->header.signature, "APIC") ||
        madt->header.length < sizeof(struct acpi_madt_header) ||
        madt->header.length > available ||
        madt->header.length > ACPI_MAX_TABLE_SIZE ||
        !checksum_valid((const uint8_t *)table_bytes, madt->header.length)) {
        return false;
    }

    parsed.local_apic_physical_address = madt->local_apic_address;
    parsed.local_apic_flags = madt->flags;
    parsed.bsp_apic_id = bsp_apic_id;
    parsed.pcat_compatible =
        (madt->flags & ACPI_MADT_FLAG_PCAT_COMPAT) != 0U;
    parsed.pit_irq_gsi = 0U;
    parsed.pit_irq_flags = 0U;
    have_local_apic_address = madt->local_apic_address != 0U;

    cursor = (const uint8_t *)(const void *)(madt + 1);
    end = (const uint8_t *)table_bytes + madt->header.length;

    while (cursor < end) {
        const struct acpi_madt_entry_header *entry =
            (const struct acpi_madt_entry_header *)(const void *)cursor;

        if (++entry_count > ACPI_MAX_MADT_ENTRIES ||
            (size_t)(end - cursor) < sizeof(*entry) ||
            entry->length < sizeof(*entry) ||
            (size_t)(end - cursor) < (size_t)entry->length) {
            return false;
        }

        switch (entry->type) {
            case ACPI_MADT_TYPE_LOCAL_APIC:
                if (entry->length < sizeof(struct acpi_madt_local_apic)) {
                    return false;
                }
                {
                    const struct acpi_madt_local_apic *local =
                        (const struct acpi_madt_local_apic *)(const void *)cursor;
                    if ((local->flags & (ACPI_CPU_ENABLED |
                                         ACPI_CPU_ONLINE_CAPABLE)) != 0U &&
                        (uint32_t)local->apic_id == bsp_apic_id) {
                        parsed.bsp_local_apic_found = true;
                    }
                }
                break;
            case ACPI_MADT_TYPE_IO_APIC:
                if (entry->length < sizeof(struct acpi_madt_io_apic)) {
                    return false;
                }
                {
                    const struct acpi_madt_io_apic *io =
                        (const struct acpi_madt_io_apic *)(const void *)cursor;
                    ++io_apic_count;
                    if (io_apic_count == 1U) {
                        parsed.io_apic_physical_address = io->address;
                        parsed.io_apic_gsi_base = io->global_irq_base;
                        parsed.io_apic_found = true;
                    }
                }
                break;
            case ACPI_MADT_TYPE_INTERRUPT_OVERRIDE:
                if (entry->length < sizeof(struct acpi_madt_interrupt_override)) {
                    return false;
                }
                {
                    const struct acpi_madt_interrupt_override *override =
                        (const struct acpi_madt_interrupt_override *)(const void *)cursor;
                    if (override->bus == 0U && override->source_irq == 0U) {
                        parsed.pit_irq_gsi = override->global_irq;
                        parsed.pit_irq_flags = override->flags;
                    }
                }
                break;
            case ACPI_MADT_TYPE_LOCAL_APIC_ADDRESS_OVERRIDE:
                if (entry->length < sizeof(struct acpi_madt_local_apic_override)) {
                    return false;
                }
                {
                    const struct acpi_madt_local_apic_override *override =
                        (const struct acpi_madt_local_apic_override *)(const void *)cursor;
                    parsed.local_apic_physical_address = override->address;
                    have_local_apic_address = override->address != 0ULL;
                }
                break;
            default:
                /* Unknown and x2APIC entries are skipped by their validated length. */
                break;
        }

        cursor += entry->length;
    }

    if (cursor != end || !have_local_apic_address ||
        !parsed.bsp_local_apic_found || !parsed.io_apic_found ||
        io_apic_count != 1U ||
        (parsed.local_apic_physical_address & 0xfffULL) != 0ULL ||
        parsed.io_apic_physical_address == 0U ||
        (parsed.io_apic_physical_address & 0xfffU) != 0U ||
        (parsed.pit_irq_flags & 0x3U) == 0x2U ||
        ((parsed.pit_irq_flags >> 2) & 0x3U) == 0x2U) {
        return false;
    }

    *platform = parsed;
    return true;
}

bool acpi_discover_platform(
    uint64_t hhdm_offset,
    const struct limine_rsdp_response *rsdp_response,
    struct acpi_platform_info *platform
) {
    const struct acpi_rsdp *rsdp = NULL;
    const struct acpi_sdt_header *root = NULL;
    const uint8_t *entries = NULL;
    uint64_t root_physical = 0ULL;
    uint64_t root_entry_size = 0ULL;
    uint64_t root_entry_count = 0ULL;
    uint32_t bsp_apic_id = 0U;
    bool use_xsdt = false;

    if (platform == NULL || rsdp_response == NULL ||
        rsdp_response->address == NULL ||
        !is_canonical_address((uint64_t)(uintptr_t)rsdp_response->address)) {
        return false;
    }

    rsdp = (const struct acpi_rsdp *)rsdp_response->address;
    if (rsdp->signature[0] != 'R' || rsdp->signature[1] != 'S' ||
        rsdp->signature[2] != 'D' || rsdp->signature[3] != ' ' ||
        rsdp->signature[4] != 'P' || rsdp->signature[5] != 'T' ||
        rsdp->signature[6] != 'R' || rsdp->signature[7] != ' ') {
        return false;
    }

    if (!checksum_valid((const uint8_t *)rsdp, ACPI_RSDP_V1_SIZE)) {
        return false;
    }

    if (rsdp->revision >= 2U) {
        if (rsdp->length < ACPI_RSDP_V2_MIN_SIZE ||
            rsdp->length > ACPI_RSDP_MAX_SIZE ||
            !checksum_valid((const uint8_t *)rsdp, rsdp->length)) {
            return false;
        }
        use_xsdt = rsdp->xsdt_address != 0ULL;
    }

    if (use_xsdt) {
        root_physical = rsdp->xsdt_address;
        root_entry_size = sizeof(uint64_t);
    } else {
        root_physical = (uint64_t)rsdp->rsdt_address;
        root_entry_size = sizeof(uint32_t);
    }

    root = (const struct acpi_sdt_header *)physical_to_hhdm(
        root_physical, hhdm_offset);
    if (root == NULL || root->length < sizeof(*root) ||
        root->length > ACPI_MAX_TABLE_SIZE ||
        !signature_equals(root->signature, use_xsdt ? "XSDT" : "RSDT") ||
        !checksum_valid((const uint8_t *)root, root->length)) {
        return false;
    }

    if (((uint64_t)root->length - sizeof(*root)) % root_entry_size != 0ULL) {
        return false;
    }
    root_entry_count = ((uint64_t)root->length - sizeof(*root)) /
                       root_entry_size;
    if (root_entry_count == 0ULL ||
        root_entry_count > ACPI_MAX_ROOT_ENTRIES) {
        return false;
    }

    entries = (const uint8_t *)(const void *)(root + 1);
    bsp_apic_id = read_bsp_apic_id();

    for (uint64_t index = 0ULL; index < root_entry_count; ++index) {
        uint64_t table_physical = 0ULL;
        const struct acpi_sdt_header *table = NULL;

        if (use_xsdt) {
            const uint64_t *table_addresses =
                (const uint64_t *)(const void *)entries;
            table_physical = table_addresses[index];
        } else {
            const uint32_t *table_addresses =
                (const uint32_t *)(const void *)entries;
            table_physical = (uint64_t)table_addresses[index];
        }

        table = (const struct acpi_sdt_header *)physical_to_hhdm(
            table_physical, hhdm_offset);
        if (table == NULL || table->length < sizeof(*table) ||
            table->length > ACPI_MAX_TABLE_SIZE ||
            !checksum_valid((const uint8_t *)table, table->length)) {
            return false;
        }

        if (signature_equals(table->signature, "APIC")) {
            return parse_madt(table, table->length, bsp_apic_id, platform);
        }
    }

    return false;
}

#ifdef DAWN_KERNEL_TEST
static uint8_t acpi_test_checksum(const uint8_t *bytes, size_t length) {
    uint8_t sum = 0U;
    for (size_t i = 0U; i < length; ++i) {
        sum = (uint8_t)(sum + bytes[i]);
    }
    return (uint8_t)(0U - sum);
}

bool acpi_parser_self_test(void) {
    uint8_t storage[128] = {0U};
    struct acpi_madt_header *madt =
        (struct acpi_madt_header *)(void *)storage;
    struct acpi_platform_info info = {0};
    const uint32_t bsp_id = read_bsp_apic_id();
    uint8_t *cursor = storage + sizeof(*madt);
    struct acpi_madt_local_apic local = {
        .header = { .type = ACPI_MADT_TYPE_LOCAL_APIC,
                    .length = (uint8_t)sizeof(struct acpi_madt_local_apic) },
        .processor_uid = 0U,
        .apic_id = (uint8_t)bsp_id,
        .flags = ACPI_CPU_ENABLED
    };
    struct acpi_madt_io_apic io = {
        .header = { .type = ACPI_MADT_TYPE_IO_APIC,
                    .length = (uint8_t)sizeof(struct acpi_madt_io_apic) },
        .io_apic_id = 0U,
        .reserved = 0U,
        .address = 0xfec00000U,
        .global_irq_base = 0U
    };
    struct acpi_madt_interrupt_override override = {
        .header = { .type = ACPI_MADT_TYPE_INTERRUPT_OVERRIDE,
                    .length = (uint8_t)sizeof(struct acpi_madt_interrupt_override) },
        .bus = 0U,
        .source_irq = 0U,
        .global_irq = 2U,
        .flags = 0x000dU
    };

    madt->header.signature[0] = 'A';
    madt->header.signature[1] = 'P';
    madt->header.signature[2] = 'I';
    madt->header.signature[3] = 'C';
    madt->header.length = (uint32_t)(sizeof(*madt) + sizeof(local) +
                                     sizeof(io) + sizeof(override));
    madt->local_apic_address = 0xfee00000U;
    madt->flags = ACPI_MADT_FLAG_PCAT_COMPAT;
    for (size_t i = 0U; i < sizeof(local); ++i) cursor[i] = ((const uint8_t *)&local)[i];
    cursor += sizeof(local);
    for (size_t i = 0U; i < sizeof(io); ++i) cursor[i] = ((const uint8_t *)&io)[i];
    cursor += sizeof(io);
    for (size_t i = 0U; i < sizeof(override); ++i) cursor[i] = ((const uint8_t *)&override)[i];
    madt->header.checksum = acpi_test_checksum(storage, madt->header.length);

    if (!parse_madt(storage, sizeof(storage), bsp_id, &info) ||
        info.local_apic_physical_address != 0xfee00000ULL ||
        info.io_apic_physical_address != 0xfec00000U ||
        info.pit_irq_gsi != 2U || info.pit_irq_flags != 0x000dU ||
        !info.pcat_compatible || !info.bsp_local_apic_found ||
        !info.io_apic_found) {
        return false;
    }

    madt->header.checksum ^= 1U;
    return !parse_madt(storage, sizeof(storage), bsp_id, &info);
}
#endif
