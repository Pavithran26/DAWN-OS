/* SPDX-License-Identifier: Unlicense */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "limine_requests.h"
#include "physical_memory.h"
#include "page_table.h"
#include "interrupts.h"
#include "acpi.h"
#include "apic.h"
#include "scheduler.h"
#include "syscall.h"
#include "user_mode.h"
#include "elf_loader.h"
#include "gdt.h"
#include "process.h"
#include "vfs.h"
#include "fat32.h"
#include "iso9660.h"
#include "vfs_cache.h"
#include "file_explorer.h"
#include "block_dev.h"
#include "pci.h"
#include "ahci.h"
#include "nvme.h"
#include "partition.h"
#include "storage_manager.h"
#include "device.h"
#include "ps2.h"
#include "xhci.h"
#include "usb_hid.h"
#include "input.h"
#include "gfx.h"
#include "window_manager.h"
#include "desktop.h"
#include "shell.h"
#include "terminal.h"
#include "task_manager.h"
#include "net.h"
#include "virtio_net.h"
#include "audio.h"
#include "settings.h"
#include "installer.h"
#include "security.h"
#include "app_runtime.h"
#include "service_manager.h"
#include "power.h"
#include "localization.h"
#include "advanced.h"
#include "os_metadata.h"
#include "system_info.h"
#include "rtc.h"
#include "calculator.h"
#include "calendar.h"
#include "smp.h"

#define SERIAL_COM1 0x3F8U
#define QEMU_DEBUG_EXIT_PORT 0xF4U
#define QEMU_TEST_PASS 0x10U
#define QEMU_TEST_FAIL 0x11U
#define SERIAL_POLL_LIMIT 100000U

#define MEMORY_PAGE_SIZE 4096ULL
#define MEMORY_MAP_MAX_ENTRIES 1024ULL
#define MEMORY_MAP_TYPE_COUNT 9U

struct memory_map_summary {
    uint64_t entry_count;
    uint64_t region_counts[MEMORY_MAP_TYPE_COUNT];
    uint64_t bytes_by_type[MEMORY_MAP_TYPE_COUNT];
    uint64_t unknown_type_count;
};

enum memory_map_error {
    MEMORY_MAP_OK = 0,
    MEMORY_MAP_INVALID_ARGUMENT,
    MEMORY_MAP_INVALID_COUNT,
    MEMORY_MAP_MISSING_ENTRY_ARRAY,
    MEMORY_MAP_NULL_ENTRY,
    MEMORY_MAP_ZERO_LENGTH,
    MEMORY_MAP_ADDRESS_OVERFLOW,
    MEMORY_MAP_UNSORTED,
    MEMORY_MAP_ALLOCATABLE_UNALIGNED,
    MEMORY_MAP_ALLOCATABLE_OVERLAP,
    MEMORY_MAP_COUNTER_OVERFLOW
};

/* Keep the request block writable: Limine writes response pointers and the
 * base-revision tag when it accepts the requested revision. */
__attribute__((used, aligned(8), section(".limine_requests_start")))
volatile uint64_t limine_requests_start[] = {
    LIMINE_REQUESTS_START_MARKER
};

__attribute__((used, aligned(8), section(".limine_requests")))
volatile uint64_t limine_base_revision[] = {
    LIMINE_BASE_REVISION(6)
};

__attribute__((used, aligned(8), section(".limine_requests")))
volatile struct limine_framebuffer_request limine_framebuffer_request = {
    .id = { LIMINE_FRAMEBUFFER_REQUEST_ID },
    .revision = 0,
    .response = NULL
};

__attribute__((used, aligned(8), section(".limine_requests")))
volatile struct limine_memmap_request limine_memmap_request = {
    .id = { LIMINE_MEMMAP_REQUEST_ID },
    .revision = 0,
    .response = NULL
};

__attribute__((used, aligned(8), section(".limine_requests")))
volatile struct limine_hhdm_request limine_hhdm_request = {
    .id = { LIMINE_HHDM_REQUEST_ID },
    .revision = 0,
    .response = NULL
};

__attribute__((used, aligned(8), section(".limine_requests")))
volatile struct limine_rsdp_request limine_rsdp_request = {
    .id = { LIMINE_RSDP_REQUEST_ID },
    .revision = 0,
    .response = NULL
};

__attribute__((used, aligned(8), section(".limine_requests")))
volatile struct limine_paging_mode_request limine_paging_mode_request = {
    .id = { LIMINE_PAGING_MODE_REQUEST_ID },
    .revision = 1,
    .response = NULL,
    .mode = LIMINE_PAGING_MODE_X86_64_4LVL,
    .max_mode = LIMINE_PAGING_MODE_X86_64_4LVL,
    .min_mode = LIMINE_PAGING_MODE_X86_64_4LVL
};

__attribute__((used, aligned(8), section(".limine_requests")))
volatile struct limine_efi_system_table_request limine_efi_system_table_request = {
    .id = { LIMINE_EFI_SYSTEM_TABLE_REQUEST_ID },
    .revision = 0,
    .response = NULL
};

__attribute__((used, aligned(8), section(".limine_requests")))
volatile struct limine_smp_request limine_smp_request = {
    .id = { LIMINE_SMP_REQUEST_ID },
    .revision = 0,
    .response = NULL,
    .flags = 0
};

__attribute__((used, aligned(8), section(".limine_requests_end")))
volatile uint64_t limine_requests_end[] = {
    LIMINE_REQUESTS_END_MARKER
};

static inline void outb(uint16_t port, uint8_t value) {
    __asm__ volatile ("outb %0, %1" : : "a"(value), "Nd"(port));
}

static inline uint8_t inb(uint16_t port) {
    uint8_t value;
    __asm__ volatile ("inb %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}

static void cpu_halt_forever(void) {
    for (;;) {
        __asm__ volatile ("cli; hlt" : : : "memory");
    }
}

static void serial_init(void) {
    outb((uint16_t)(SERIAL_COM1 + 1U), 0x00U);
    outb((uint16_t)(SERIAL_COM1 + 3U), 0x80U);
    outb((uint16_t)SERIAL_COM1, 0x01U);
    outb((uint16_t)(SERIAL_COM1 + 1U), 0x00U);
    outb((uint16_t)(SERIAL_COM1 + 3U), 0x03U);
    outb((uint16_t)(SERIAL_COM1 + 2U), 0xC7U);
    outb((uint16_t)(SERIAL_COM1 + 4U), 0x0BU);
}

static bool serial_write_char(char value) {
    for (uint32_t attempt = 0; attempt < SERIAL_POLL_LIMIT; ++attempt) {
        if ((inb((uint16_t)(SERIAL_COM1 + 5U)) & 0x20U) != 0U) {
            outb((uint16_t)SERIAL_COM1, (uint8_t)value);
            return true;
        }
    }
    return false;
}

static void serial_write(const char *text) {
    while (*text != '\0') {
        if (*text == '\n' && !serial_write_char('\r')) {
            return;
        }
        if (!serial_write_char(*text)) {
            return;
        }
        ++text;
    }
}

static void serial_write_u64(uint64_t value) {
    char digits[20];
    size_t count = 0U;

    do {
        const uint64_t digit = value % 10ULL;
        digits[count] = (char)('0' + (char)digit);
        ++count;
        value /= 10ULL;
    } while (value != 0ULL && count < sizeof(digits));

    while (count > 0U) {
        --count;
        if (!serial_write_char(digits[count])) {
            return;
        }
    }
}

static bool add_u64_checked(uint64_t left, uint64_t right, uint64_t *result) {
    if (result == NULL || right > UINT64_MAX - left) {
        return false;
    }
    *result = left + right;
    return true;
}

static bool protocol_revision_supported(void) {
    return LIMINE_BASE_REVISION_SUPPORTED(limine_base_revision);
}

static bool hhdm_response_valid(const struct limine_hhdm_response *response,
                                uint64_t highest_usable_end) {
    uint64_t last_virtual = 0ULL;
    if (response == NULL || (response->offset % MEMORY_PAGE_SIZE) != 0ULL) {
        return false;
    }
    /* Never assume a fixed HHDM offset; Limine selects it at boot. */
    return add_u64_checked(response->offset, highest_usable_end, &last_virtual);
}

static uint64_t memory_map_highest_usable_end(
    const struct limine_memmap_response *response) {
    uint64_t highest = 0ULL;
    if (response == NULL || response->entries == NULL ||
        response->entry_count > MEMORY_MAP_MAX_ENTRIES) return 0ULL;
    for (uint64_t i = 0ULL; i < response->entry_count; ++i) {
        const struct limine_memmap_entry *entry = response->entries[i];
        if (entry != NULL && entry->type == LIMINE_MEMMAP_USABLE &&
            entry->length <= UINT64_MAX - entry->base &&
            entry->base + entry->length > highest) {
            highest = entry->base + entry->length;
        }
    }
    return highest;
}

static enum memory_map_error validate_memory_map(
    const struct limine_memmap_response *response,
    struct memory_map_summary *summary
) {
    struct memory_map_summary candidate = {0};
    uint64_t previous_base = 0ULL;
    uint64_t highest_end = 0ULL;
    uint64_t highest_allocatable_end = 0ULL;
    bool have_previous = false;

    if (response == NULL || summary == NULL) {
        return MEMORY_MAP_INVALID_ARGUMENT;
    }
    if (response->entry_count == 0ULL ||
        response->entry_count > MEMORY_MAP_MAX_ENTRIES) {
        return MEMORY_MAP_INVALID_COUNT;
    }
    if (response->entries == NULL) {
        return MEMORY_MAP_MISSING_ENTRY_ARRAY;
    }

    candidate.entry_count = response->entry_count;

    for (uint64_t i = 0ULL; i < response->entry_count; ++i) {
        const struct limine_memmap_entry *entry = response->entries[i];
        uint64_t region_end = 0ULL;
        const bool allocatable =
            entry != NULL &&
            (entry->type == LIMINE_MEMMAP_USABLE ||
             entry->type == LIMINE_MEMMAP_BOOTLOADER_RECLAIMABLE);

        if (entry == NULL) {
            return MEMORY_MAP_NULL_ENTRY;
        }
        if (entry->length == 0ULL) {
            return MEMORY_MAP_ZERO_LENGTH;
        }
        if (entry->length > UINT64_MAX - entry->base) {
            return MEMORY_MAP_ADDRESS_OVERFLOW;
        }
        region_end = entry->base + entry->length;

        /* Limine guarantees ascending base addresses. Equal bases are allowed
         * for non-allocatable ranges because those ranges may overlap. */
        if (have_previous && entry->base < previous_base) {
            return MEMORY_MAP_UNSORTED;
        }

        if (allocatable) {
            if ((entry->base % MEMORY_PAGE_SIZE) != 0ULL ||
                (entry->length % MEMORY_PAGE_SIZE) != 0ULL) {
                return MEMORY_MAP_ALLOCATABLE_UNALIGNED;
            }
            /* Usable and bootloader-reclaimable regions must not overlap any
             * prior region, including non-allocatable firmware reservations. */
            if (entry->base < highest_end) {
                return MEMORY_MAP_ALLOCATABLE_OVERLAP;
            }
        } else if (entry->base < highest_allocatable_end) {
            /* Non-allocatable ranges may overlap each other, but may not
             * overlap any usable or bootloader-reclaimable range. */
            return MEMORY_MAP_ALLOCATABLE_OVERLAP;
        }

        if (region_end > highest_end) {
            highest_end = region_end;
        }
        if (allocatable && region_end > highest_allocatable_end) {
            highest_allocatable_end = region_end;
        }
        previous_base = entry->base;
        have_previous = true;

        if (entry->type < (uint64_t)MEMORY_MAP_TYPE_COUNT) {
            const size_t type_index = (size_t)entry->type;
            uint64_t updated_bytes = 0ULL;

            if (!add_u64_checked(candidate.bytes_by_type[type_index],
                                 entry->length, &updated_bytes)) {
                return MEMORY_MAP_COUNTER_OVERFLOW;
            }
            candidate.bytes_by_type[type_index] = updated_bytes;
            ++candidate.region_counts[type_index];
        } else {
            /* Unknown future types are accepted but never considered usable. */
            ++candidate.unknown_type_count;
        }
    }

    *summary = candidate;
    return MEMORY_MAP_OK;
}

static const char *memory_map_error_string(enum memory_map_error error) {
    switch (error) {
        case MEMORY_MAP_OK:
            return "none";
        case MEMORY_MAP_INVALID_ARGUMENT:
            return "null response or summary";
        case MEMORY_MAP_INVALID_COUNT:
            return "entry count is zero or exceeds safety limit";
        case MEMORY_MAP_MISSING_ENTRY_ARRAY:
            return "entry pointer array is null";
        case MEMORY_MAP_NULL_ENTRY:
            return "entry pointer is null";
        case MEMORY_MAP_ZERO_LENGTH:
            return "region length is zero";
        case MEMORY_MAP_ADDRESS_OVERFLOW:
            return "base plus length overflows";
        case MEMORY_MAP_UNSORTED:
            return "entries are not sorted by base address";
        case MEMORY_MAP_ALLOCATABLE_UNALIGNED:
            return "usable/reclaimable region is not page-aligned";
        case MEMORY_MAP_ALLOCATABLE_OVERLAP:
            return "allocatable region overlaps another region";
        case MEMORY_MAP_COUNTER_OVERFLOW:
            return "memory-region byte counter overflows";
        default:
            return "unknown validation error";
    }
}

static void serial_write_memory_map_summary(
    const struct memory_map_summary *summary
) {
    serial_write("DAWN-OS: memory map entries: ");
    serial_write_u64(summary->entry_count);
    serial_write("\nDAWN-OS: usable regions: ");
    serial_write_u64(summary->region_counts[LIMINE_MEMMAP_USABLE]);
    serial_write("\nDAWN-OS: usable RAM MiB: ");
    serial_write_u64(summary->bytes_by_type[LIMINE_MEMMAP_USABLE] /
                     (1024ULL * 1024ULL));
    serial_write("\nDAWN-OS: bootloader-reclaimable regions: ");
    serial_write_u64(
        summary->region_counts[LIMINE_MEMMAP_BOOTLOADER_RECLAIMABLE]);
    serial_write("\nDAWN-OS: bootloader-reclaimable MiB: ");
    serial_write_u64(
        summary->bytes_by_type[LIMINE_MEMMAP_BOOTLOADER_RECLAIMABLE] /
        (1024ULL * 1024ULL));
    serial_write("\nDAWN-OS: unknown memory-map types (not allocatable): ");
    serial_write_u64(summary->unknown_type_count);
    serial_write("\n");
}

#ifdef DAWN_KERNEL_TEST
static bool memory_map_expect_error(
    const struct limine_memmap_response *response,
    enum memory_map_error expected
) {
    struct memory_map_summary summary = {0};
    return validate_memory_map(response, &summary) == expected;
}

static bool memory_map_validator_self_test(void) {
    struct memory_map_summary summary = {0};

    struct limine_memmap_entry usable = {
        .base = 0x1000ULL,
        .length = 0x2000ULL,
        .type = LIMINE_MEMMAP_USABLE
    };
    struct limine_memmap_entry reserved = {
        .base = 0x4000ULL,
        .length = 0x1000ULL,
        .type = LIMINE_MEMMAP_RESERVED
    };
    struct limine_memmap_entry reclaimable = {
        .base = 0x5000ULL,
        .length = 0x1000ULL,
        .type = LIMINE_MEMMAP_BOOTLOADER_RECLAIMABLE
    };
    struct limine_memmap_entry *valid_entries[] = {
        &usable, &reserved, &reclaimable
    };
    struct limine_memmap_response valid_response = {
        .revision = 0ULL,
        .entry_count = 3ULL,
        .entries = valid_entries
    };

    if (validate_memory_map(&valid_response, &summary) != MEMORY_MAP_OK ||
        summary.entry_count != 3ULL ||
        summary.region_counts[LIMINE_MEMMAP_USABLE] != 1ULL ||
        summary.bytes_by_type[LIMINE_MEMMAP_USABLE] != 0x2000ULL ||
        summary.region_counts[LIMINE_MEMMAP_BOOTLOADER_RECLAIMABLE] != 1ULL) {
        return false;
    }

    struct limine_memmap_response invalid_count = valid_response;
    invalid_count.entry_count = MEMORY_MAP_MAX_ENTRIES + 1ULL;
    if (!memory_map_expect_error(&invalid_count, MEMORY_MAP_INVALID_COUNT)) {
        return false;
    }

    struct limine_memmap_response missing_array = valid_response;
    missing_array.entries = NULL;
    if (!memory_map_expect_error(&missing_array,
                                 MEMORY_MAP_MISSING_ENTRY_ARRAY)) {
        return false;
    }

    struct limine_memmap_entry *null_entry_array[] = { NULL };
    struct limine_memmap_response null_entry = {
        .revision = 0ULL, .entry_count = 1ULL, .entries = null_entry_array
    };
    if (!memory_map_expect_error(&null_entry, MEMORY_MAP_NULL_ENTRY)) {
        return false;
    }

    struct limine_memmap_entry zero_length = {
        .base = 0x1000ULL, .length = 0ULL, .type = LIMINE_MEMMAP_RESERVED
    };
    struct limine_memmap_entry *zero_length_array[] = { &zero_length };
    struct limine_memmap_response zero_length_response = {
        .revision = 0ULL, .entry_count = 1ULL, .entries = zero_length_array
    };
    if (!memory_map_expect_error(&zero_length_response,
                                 MEMORY_MAP_ZERO_LENGTH)) {
        return false;
    }

    struct limine_memmap_entry overflowing = {
        .base = UINT64_MAX - 0x7ffULL,
        .length = 0x1000ULL,
        .type = LIMINE_MEMMAP_RESERVED
    };
    struct limine_memmap_entry *overflowing_array[] = { &overflowing };
    struct limine_memmap_response overflowing_response = {
        .revision = 0ULL, .entry_count = 1ULL, .entries = overflowing_array
    };
    if (!memory_map_expect_error(&overflowing_response,
                                 MEMORY_MAP_ADDRESS_OVERFLOW)) {
        return false;
    }

    struct limine_memmap_entry high_reserved = {
        .base = 0x5000ULL, .length = 0x1000ULL,
        .type = LIMINE_MEMMAP_RESERVED
    };
    struct limine_memmap_entry low_reserved = {
        .base = 0x4000ULL, .length = 0x1000ULL,
        .type = LIMINE_MEMMAP_RESERVED
    };
    struct limine_memmap_entry *unsorted_array[] = {
        &high_reserved, &low_reserved
    };
    struct limine_memmap_response unsorted_response = {
        .revision = 0ULL, .entry_count = 2ULL, .entries = unsorted_array
    };
    if (!memory_map_expect_error(&unsorted_response, MEMORY_MAP_UNSORTED)) {
        return false;
    }

    struct limine_memmap_entry unaligned_usable = {
        .base = 0x1001ULL, .length = 0x1000ULL,
        .type = LIMINE_MEMMAP_USABLE
    };
    struct limine_memmap_entry *unaligned_array[] = { &unaligned_usable };
    struct limine_memmap_response unaligned_response = {
        .revision = 0ULL, .entry_count = 1ULL, .entries = unaligned_array
    };
    if (!memory_map_expect_error(&unaligned_response,
                                 MEMORY_MAP_ALLOCATABLE_UNALIGNED)) {
        return false;
    }

    struct limine_memmap_entry overlap_usable = {
        .base = 0x1000ULL, .length = 0x3000ULL,
        .type = LIMINE_MEMMAP_USABLE
    };
    struct limine_memmap_entry overlap_reserved = {
        .base = 0x2000ULL, .length = 0x1000ULL,
        .type = LIMINE_MEMMAP_RESERVED
    };
    struct limine_memmap_entry *overlap_array[] = {
        &overlap_usable, &overlap_reserved
    };
    struct limine_memmap_response overlap_response = {
        .revision = 0ULL, .entry_count = 2ULL, .entries = overlap_array
    };
    if (!memory_map_expect_error(&overlap_response,
                                 MEMORY_MAP_ALLOCATABLE_OVERLAP)) {
        return false;
    }

    /* Overlap between non-allocatable firmware reservations is permitted by
     * the protocol and must not be rejected by our validator. */
    struct limine_memmap_entry reserved_outer = {
        .base = 0x1000ULL, .length = 0x4000ULL,
        .type = LIMINE_MEMMAP_RESERVED
    };
    struct limine_memmap_entry reserved_inner = {
        .base = 0x2000ULL, .length = 0x1000ULL,
        .type = LIMINE_MEMMAP_ACPI_NVS
    };
    struct limine_memmap_entry *reserved_overlap_array[] = {
        &reserved_outer, &reserved_inner
    };
    struct limine_memmap_response reserved_overlap_response = {
        .revision = 0ULL, .entry_count = 2ULL,
        .entries = reserved_overlap_array
    };
    if (validate_memory_map(&reserved_overlap_response, &summary) !=
        MEMORY_MAP_OK) {
        return false;
    }

    /* Unknown future types are visible in the summary but never usable RAM. */
    struct limine_memmap_entry unknown_type = {
        .base = 0x8000ULL, .length = 0x1000ULL, .type = UINT64_MAX
    };
    struct limine_memmap_entry *unknown_array[] = { &unknown_type };
    struct limine_memmap_response unknown_response = {
        .revision = 0ULL, .entry_count = 1ULL, .entries = unknown_array
    };
    if (validate_memory_map(&unknown_response, &summary) != MEMORY_MAP_OK ||
        summary.unknown_type_count != 1ULL ||
        summary.bytes_by_type[LIMINE_MEMMAP_USABLE] != 0ULL) {
        return false;
    }

    return true;
}
#endif

#ifdef DAWN_KERNEL_TEST
static void qemu_debug_exit(uint8_t code) {
    outb((uint16_t)QEMU_DEBUG_EXIT_PORT, code);
}
#endif

void kernel_main(void) {
    struct memory_map_summary memory_summary = {0};
    enum memory_map_error memory_error = MEMORY_MAP_OK;

    serial_init();
    serial_write("DAWN-OS: kernel entry reached\n");
    serial_write("DAWN-OS: x86_64 freestanding bootstrap\n");

    if (!protocol_revision_supported()) {
        serial_write("DAWN-OS: FAIL: Limine base revision 6 was not accepted\n");
#ifdef DAWN_KERNEL_TEST
        qemu_debug_exit(QEMU_TEST_FAIL);
#endif
        cpu_halt_forever();
    }
    serial_write("DAWN-OS: Limine base revision 6 accepted\n");

    if (limine_paging_mode_request.response == NULL ||
        limine_paging_mode_request.response->mode !=
            LIMINE_PAGING_MODE_X86_64_4LVL) {
        serial_write("DAWN-OS: FAIL: Limine did not provide requested 4-level paging mode\n");
#ifdef DAWN_KERNEL_TEST
        qemu_debug_exit(QEMU_TEST_FAIL);
#endif
        cpu_halt_forever();
    }
    serial_write("DAWN-OS: Limine 4-level paging mode confirmed\n");

#ifdef DAWN_KERNEL_TEST
    if (!memory_map_validator_self_test()) {
        serial_write("DAWN-TEST: FAIL: memory-map validator edge cases\n");
        qemu_debug_exit(QEMU_TEST_FAIL);
        cpu_halt_forever();
    }
    serial_write("DAWN-TEST: PASS: memory-map validator edge cases\n");
#endif

    memory_error = validate_memory_map(limine_memmap_request.response,
                                       &memory_summary);
    if (memory_error != MEMORY_MAP_OK ||
        memory_summary.region_counts[LIMINE_MEMMAP_USABLE] == 0ULL) {
        serial_write("DAWN-OS: FAIL: Limine memory map rejected: ");
        serial_write(memory_map_error_string(memory_error));
        serial_write("\n");
#ifdef DAWN_KERNEL_TEST
        qemu_debug_exit(QEMU_TEST_FAIL);
#endif
        cpu_halt_forever();
    }
    serial_write("DAWN-OS: Limine memory map validated\n");
    serial_write_memory_map_summary(&memory_summary);

    if (!hhdm_response_valid(limine_hhdm_request.response,
                             memory_map_highest_usable_end(limine_memmap_request.response))) {
        serial_write("DAWN-OS: FAIL: Limine HHDM response missing or invalid\n");
#ifdef DAWN_KERNEL_TEST
        qemu_debug_exit(QEMU_TEST_FAIL);
#endif
        cpu_halt_forever();
    }
    serial_write("DAWN-OS: HHDM response validated; dynamic offset accepted\n");

#ifdef DAWN_KERNEL_TEST
    if (!physical_allocator_self_test()) {
        serial_write("DAWN-TEST: FAIL: physical page allocator edge cases\n");
        qemu_debug_exit(QEMU_TEST_FAIL);
        cpu_halt_forever();
    }
    serial_write("DAWN-TEST: PASS: physical page allocator edge cases\n");
#endif

    if (!physical_allocator_initialize(limine_memmap_request.response)) {
        serial_write("DAWN-OS: FAIL: physical page allocator initialization\n");
#ifdef DAWN_KERNEL_TEST
        qemu_debug_exit(QEMU_TEST_FAIL);
#endif
        cpu_halt_forever();
    }
    struct physical_allocator_stats allocator_stats = {0};
    physical_allocator_get_stats(&allocator_stats);
    serial_write("DAWN-OS: physical allocator managed frames: ");
    serial_write_u64(allocator_stats.managed_frames);
    serial_write("\nDAWN-OS: physical allocator free frames: ");
    serial_write_u64(allocator_stats.free_frames);
    serial_write("\nDAWN-OS: physical allocator tracking limit MiB: ");
    serial_write_u64(PHYSICAL_ALLOCATOR_MAX_ADDRESS / (1024ULL * 1024ULL));
    serial_write("\n");
    if (!vm_manager_initialize(limine_hhdm_request.response->offset)) {
        serial_write("DAWN-OS: FAIL: x86_64 page-table manager initialization\n");
#ifdef DAWN_KERNEL_TEST
        qemu_debug_exit(QEMU_TEST_FAIL);
#endif
        cpu_halt_forever();
    }
    serial_write("DAWN-OS: x86_64 four-level page-table manager initialized\n");

#ifdef DAWN_KERNEL_TEST
    if (!vm_manager_self_test()) {
        serial_write("DAWN-TEST: FAIL: virtual-memory map/translate/unmap tests\n");
        qemu_debug_exit(QEMU_TEST_FAIL);
        cpu_halt_forever();
    }
    serial_write("DAWN-TEST: PASS: virtual-memory map/translate/unmap tests\n");
    if (!vm_address_space_self_test()) {
        serial_write("DAWN-TEST: FAIL: isolated process address-space tests\n");
        qemu_debug_exit(QEMU_TEST_FAIL);
        cpu_halt_forever();
    }
    serial_write("DAWN-TEST: PASS: separate user roots, private user mappings and address-space teardown\n");

    if (!syscall_self_test()) {
        serial_write("DAWN-TEST: FAIL: Phase C syscall ABI validation\n");
        qemu_debug_exit(QEMU_TEST_FAIL);
        cpu_halt_forever();
    }
    serial_write("DAWN-TEST: PASS: kernel-callable syscall dispatcher and user-memory helper unit tests (no ring-3 entry)\n");

#endif

    if (!gdt_tss_initialize()) {
        serial_write("DAWN-OS: FAIL: GDT/TSS initialization\n");
#ifdef DAWN_KERNEL_TEST
        qemu_debug_exit(QEMU_TEST_FAIL);
#endif
        cpu_halt_forever();
    }
    serial_write("DAWN-OS: kernel GDT and 64-bit TSS loaded; privilege stack ready\n");

    if (!syscall_init_hardware()) {
        serial_write("DAWN-OS: FAIL: hardware syscall MSR initialization\n");
#ifdef DAWN_KERNEL_TEST
        qemu_debug_exit(QEMU_TEST_FAIL);
#endif
        cpu_halt_forever();
    }
    serial_write("DAWN-OS: x86-64 hardware fast syscall (MSR STAR/LSTAR/FMASK) initialized\n");
    if (!process_manager_initialize()) {
        serial_write("DAWN-OS: FAIL: process resource manager initialization\n");
#ifdef DAWN_KERNEL_TEST
        qemu_debug_exit(QEMU_TEST_FAIL);
#endif
        cpu_halt_forever();
    }
#ifdef DAWN_KERNEL_TEST
    if (!process_manager_self_test()) {
        serial_write("DAWN-TEST: FAIL: process resource lifecycle tests\n");
        qemu_debug_exit(QEMU_TEST_FAIL);
        cpu_halt_forever();
    }
    serial_write("DAWN-TEST: PASS: process address-space and kernel-stack ownership, lifecycle, reaping and slot reuse\n");
#endif
#ifdef DAWN_KERNEL_TEST
    if (!user_mode_self_test()) {
        serial_write("DAWN-TEST: FAIL: user-mode entry argument validation\n");
        qemu_debug_exit(QEMU_TEST_FAIL);
        cpu_halt_forever();
    }
    serial_write("DAWN-TEST: PASS: user-mode entry validates active process mappings and stack alignment\n");
    if (!elf64_loader_self_test()) {
        serial_write("DAWN-TEST: FAIL: ELF64 executable validation\n");
        qemu_debug_exit(QEMU_TEST_FAIL);
        cpu_halt_forever();
    }
    serial_write("DAWN-TEST: PASS: ELF64 header, load segments, permissions and bounds validation\n");
#endif

#ifdef DAWN_KERNEL_TEST
    if (!gdt_tss_self_test()) {
        serial_write("DAWN-TEST: FAIL: GDT/TSS selector and stack validation\n");
        qemu_debug_exit(QEMU_TEST_FAIL);
        cpu_halt_forever();
    }
    serial_write("DAWN-TEST: PASS: GDT/TSS descriptor, TR and RSP0 validation\n");
#endif

    if (!interrupts_initialize()) {
        serial_write("DAWN-OS: FAIL: IDT/PIC initialization\n");
#ifdef DAWN_KERNEL_TEST
        qemu_debug_exit(QEMU_TEST_FAIL);
#endif
        cpu_halt_forever();
    }
    serial_write("DAWN-OS: 256-entry IDT loaded; legacy PIC remapped and masked\n");

    if (!vfs_initialize()) {
        serial_write("DAWN-OS: FAIL: VFS initialization\n");
#ifdef DAWN_KERNEL_TEST
        qemu_debug_exit(QEMU_TEST_FAIL);
#endif
        cpu_halt_forever();
    }
    serial_write("DAWN-OS: VFS root, mounts and standard directory hierarchy initialized\n");

    if (!storage_manager_initialize()) {
        serial_write("DAWN-OS: FAIL: storage manager initialization\n");
#ifdef DAWN_KERNEL_TEST
        qemu_debug_exit(QEMU_TEST_FAIL);
#endif
        cpu_halt_forever();
    }
    serial_write("DAWN-OS: storage manager, block devices, PCI storage, AHCI, NVMe and partitions initialized\n");

    if (!device_manager_initialize()) {
        serial_write("DAWN-OS: FAIL: device manager initialization\n");
#ifdef DAWN_KERNEL_TEST
        qemu_debug_exit(QEMU_TEST_FAIL);
#endif
        cpu_halt_forever();
    }
    serial_write("DAWN-OS: device manager and driver framework initialized\n");

    if (!rtc_initialize()) {
        serial_write("DAWN-OS: FAIL: RTC driver initialization\n");
#ifdef DAWN_KERNEL_TEST
        qemu_debug_exit(QEMU_TEST_FAIL);
#endif
        cpu_halt_forever();
    }
    serial_write("DAWN-OS: CMOS real-time clock driver initialized\n");

    if (!input_initialize()) {
        serial_write("DAWN-OS: FAIL: input subsystem initialization\n");
#ifdef DAWN_KERNEL_TEST
        qemu_debug_exit(QEMU_TEST_FAIL);
#endif
        cpu_halt_forever();
    }
    serial_write("DAWN-OS: input subsystem, PS/2 fallback, xHCI and USB HID initialized\n");

    if (!gfx_initialize()) {
        serial_write("DAWN-OS: FAIL: graphics engine initialization\n");
#ifdef DAWN_KERNEL_TEST
        qemu_debug_exit(QEMU_TEST_FAIL);
#endif
        cpu_halt_forever();
    }
    serial_write("DAWN-OS: graphics abstraction layer and 2D drawing engine ready\n");

    if (!wm_initialize()) {
        serial_write("DAWN-OS: FAIL: window manager initialization\n");
#ifdef DAWN_KERNEL_TEST
        qemu_debug_exit(QEMU_TEST_FAIL);
#endif
        cpu_halt_forever();
    }
    serial_write("DAWN-OS: window manager and compositor ready\n");

    if (!desktop_initialize()) {
        serial_write("DAWN-OS: FAIL: desktop environment initialization\n");
#ifdef DAWN_KERNEL_TEST
        qemu_debug_exit(QEMU_TEST_FAIL);
#endif
        cpu_halt_forever();
    }
    serial_write("DAWN-OS: Emerald Green Neo-Liquid Glass desktop environment initialized\n");

    if (!terminal_initialize()) {
        serial_write("DAWN-OS: FAIL: terminal emulator initialization\n");
#ifdef DAWN_KERNEL_TEST
        qemu_debug_exit(QEMU_TEST_FAIL);
#endif
        cpu_halt_forever();
    }
    serial_write("DAWN-OS: terminal emulator and shell engine ready\n");

    if (!task_manager_initialize()) {
        serial_write("DAWN-OS: FAIL: task manager initialization\n");
#ifdef DAWN_KERNEL_TEST
        qemu_debug_exit(QEMU_TEST_FAIL);
#endif
        cpu_halt_forever();
    }
    serial_write("DAWN-OS: task manager and resource monitor ready\n");

    if (!net_initialize()) {
        serial_write("DAWN-OS: FAIL: network subsystem initialization\n");
#ifdef DAWN_KERNEL_TEST
        qemu_debug_exit(QEMU_TEST_FAIL);
#endif
        cpu_halt_forever();
    }
    serial_write("DAWN-OS: network stack, loopback, ARP, IPv4, UDP, TCP and sockets ready\n");

    if (!audio_initialize()) {
        serial_write("DAWN-OS: FAIL: audio subsystem initialization\n");
#ifdef DAWN_KERNEL_TEST
        qemu_debug_exit(QEMU_TEST_FAIL);
#endif
        cpu_halt_forever();
    }
    serial_write("DAWN-OS: audio subsystem, PCI audio, mixer and tone generator ready\n");

    if (!settings_initialize()) {
        serial_write("DAWN-OS: FAIL: settings subsystem initialization\n");
#ifdef DAWN_KERNEL_TEST
        qemu_debug_exit(QEMU_TEST_FAIL);
#endif
        cpu_halt_forever();
    }
    serial_write("DAWN-OS: settings subsystem, persistent configuration and UI controls ready\n");

    if (!installer_initialize()) {
        serial_write("DAWN-OS: FAIL: installer and recovery initialization\n");
#ifdef DAWN_KERNEL_TEST
        qemu_debug_exit(QEMU_TEST_FAIL);
#endif
        cpu_halt_forever();
    }
    serial_write("DAWN-OS: installer workflow, partition deployment and rescue shell ready\n");

    if (!security_initialize()) {
        serial_write("DAWN-OS: FAIL: security subsystem initialization\n");
#ifdef DAWN_KERNEL_TEST
        qemu_debug_exit(QEMU_TEST_FAIL);
#endif
        cpu_halt_forever();
    }
    serial_write("DAWN-OS: security subsystem, user accounts, authentication and audit logging ready\n");

    if (!app_runtime_initialize()) {
        serial_write("DAWN-OS: FAIL: application runtime initialization\n");
#ifdef DAWN_KERNEL_TEST
        qemu_debug_exit(QEMU_TEST_FAIL);
#endif
        cpu_halt_forever();
    }
    serial_write("DAWN-OS: application runtime, dlibc heap pool and package manager ready\n");

    if (!service_manager_initialize()) {
        serial_write("DAWN-OS: FAIL: service manager initialization\n");
#ifdef DAWN_KERNEL_TEST
        qemu_debug_exit(QEMU_TEST_FAIL);
#endif
        cpu_halt_forever();
    }
    serial_write("DAWN-OS: service manager, background daemons, IPC bus and event dispatch ready\n");

    if (!power_initialize()) {
        serial_write("DAWN-OS: FAIL: power management initialization\n");
#ifdef DAWN_KERNEL_TEST
        qemu_debug_exit(QEMU_TEST_FAIL);
#endif
        cpu_halt_forever();
    }
    serial_write("DAWN-OS: ACPI power management, reboot/shutdown and panic diagnostics ready\n");

    if (!localization_initialize()) {
        serial_write("DAWN-OS: FAIL: localization initialization\n");
#ifdef DAWN_KERNEL_TEST
        qemu_debug_exit(QEMU_TEST_FAIL);
#endif
        cpu_halt_forever();
    }
    serial_write("DAWN-OS: localization, multi-language catalog, keymaps and accessibility ready\n");

    if (!advanced_features_initialize()) {
        serial_write("DAWN-OS: FAIL: advanced features initialization\n");
#ifdef DAWN_KERNEL_TEST
        qemu_debug_exit(QEMU_TEST_FAIL);
#endif
        cpu_halt_forever();
    }
    serial_write("DAWN-OS: advanced features, spinlocks, ticketlocks, semaphores, shm, mmap and modules ready\n");

    if (!dawn_metadata_initialize()) {
        serial_write("DAWN-OS: FAIL: centralized OS metadata validation\n");
#ifdef DAWN_KERNEL_TEST
        qemu_debug_exit(QEMU_TEST_FAIL);
#endif
        cpu_halt_forever();
    }
    serial_write("DAWN-OS: centralized OS metadata validated\n");

    {
        struct dawn_system_info system_info;
        if (!dawn_system_info_refresh(&system_info)) {
            serial_write("DAWN-OS: FAIL: system information provider\n");
#ifdef DAWN_KERNEL_TEST
            qemu_debug_exit(QEMU_TEST_FAIL);
#endif
            cpu_halt_forever();
        }
    }
    serial_write("DAWN-OS: system information provider ready\n");

#ifdef DAWN_KERNEL_TEST
    if (!interrupts_self_test()) {
        serial_write("DAWN-TEST: FAIL: exception/IDT/PIT timer tests\n");
        qemu_debug_exit(QEMU_TEST_FAIL);
        cpu_halt_forever();
    }
    serial_write("DAWN-TEST: PASS: breakpoint, PIC mask, IRQ-vector dispatch and PIT counter tests\n");
#endif

#ifdef DAWN_KERNEL_TEST
    if (!acpi_parser_self_test()) {
        serial_write("DAWN-TEST: FAIL: ACPI MADT parser edge cases\n");
        qemu_debug_exit(QEMU_TEST_FAIL);
        cpu_halt_forever();
    }
    serial_write("DAWN-TEST: PASS: ACPI MADT parser edge cases\n");
#endif

    struct acpi_platform_info acpi_platform = {0};
    if (!acpi_discover_platform(limine_hhdm_request.response->offset,
                                limine_rsdp_request.response,
                                &acpi_platform)) {
        serial_write("DAWN-OS: ACPI MADT unavailable or invalid; external IRQs remain disabled\n");
#ifdef DAWN_KERNEL_TEST
        serial_write("DAWN-TEST: FAIL: ACPI MADT discovery\n");
        qemu_debug_exit(QEMU_TEST_FAIL);
        cpu_halt_forever();
#endif
    } else {
        serial_write("DAWN-OS: ACPI MADT: BSP APIC ID ");
        serial_write_u64(acpi_platform.bsp_apic_id);
        serial_write("\nDAWN-OS: ACPI MADT: IOAPIC GSI base ");
        serial_write_u64(acpi_platform.io_apic_gsi_base);
        serial_write("\nDAWN-OS: ACPI MADT: PIT IRQ0 override GSI ");
        serial_write_u64(acpi_platform.pit_irq_gsi);
        serial_write("\n");

        if (!apic_initialize(&acpi_platform)) {
            serial_write("DAWN-OS: APIC/IOAPIC initialization rejected; external IRQs remain disabled\n");
#ifdef DAWN_KERNEL_TEST
            serial_write("DAWN-TEST: FAIL: APIC/IOAPIC initialization\n");
            qemu_debug_exit(QEMU_TEST_FAIL);
            cpu_halt_forever();
#endif
        } else {
            serial_write("DAWN-OS: xAPIC and IOAPIC initialized; all IOAPIC routes masked\n");
#ifdef DAWN_KERNEL_TEST
            if (!apic_route_pit_irq_test(acpi_platform.pit_irq_gsi,
                                         acpi_platform.pit_irq_flags)) {
                serial_write("DAWN-TEST: FAIL: real IOAPIC PIT IRQ delivery\n");
                qemu_debug_exit(QEMU_TEST_FAIL);
                cpu_halt_forever();
            }
            serial_write("DAWN-TEST: PASS: real IOAPIC-routed PIT IRQ delivery\n");
            if (!apic_local_timer_self_test()) {
                serial_write("DAWN-TEST: FAIL: Local APIC one-shot timer interrupt\n");
                qemu_debug_exit(QEMU_TEST_FAIL);
                cpu_halt_forever();
            }
            serial_write("DAWN-TEST: PASS: Local APIC one-shot timer interrupt\n");
#endif
            if (apic_local_timer_calibrate(100U)) {
                serial_write("DAWN-OS: Local APIC timer calibrated for ");
                serial_write_u64(apic_local_timer_calibrated_hz());
                serial_write(" Hz; initial count ");
                serial_write_u64(apic_local_timer_calibrated_period_count());
                serial_write("\n");
#ifdef DAWN_KERNEL_TEST
                if (!apic_local_timer_calibration_tolerance_test()) {
                    serial_write("DAWN-TEST: FAIL: Local APIC timer calibration accuracy and repeatability tolerance\n");
                    qemu_debug_exit(QEMU_TEST_FAIL);
                    cpu_halt_forever();
                }
                serial_write("DAWN-TEST: PASS: Local APIC timer calibration accuracy and repeatability tolerance\n");
                serial_write("DAWN-TEST: PASS: Local APIC timer calibration\n");
#endif
            } else {
                serial_write("DAWN-OS: WARNING: Local APIC timer calibration failed\n");
#ifdef DAWN_KERNEL_TEST
                serial_write("DAWN-TEST: FAIL: Local APIC timer calibration against PIT channel 2\n");
                qemu_debug_exit(QEMU_TEST_FAIL);
                cpu_halt_forever();
#endif
            }
        }
    }

    if (!smp_initialize()) {
        serial_write("DAWN-OS: WARNING: SMP multi-core initialization failed\n");
    } else {
        serial_write("DAWN-OS: SMP initialized; detected CPU count: ");
        serial_write_u64(smp_cpu_count());
        serial_write(", online: ");
        serial_write_u64(smp_online_cpu_count());
        serial_write("\n");
#ifdef DAWN_KERNEL_TEST
        if (!smp_self_test()) {
            serial_write("DAWN-TEST: FAIL: SMP multi-core initialization and CPU state\n");
            qemu_debug_exit(QEMU_TEST_FAIL);
            cpu_halt_forever();
        }
        serial_write("DAWN-TEST: PASS: SMP multi-core detection, BSP initialization and AP boot\n");
#endif
    }

    if (!scheduler_initialize()) {
        serial_write("DAWN-OS: FAIL: kernel-thread scheduler initialization\n");
#ifdef DAWN_KERNEL_TEST
        qemu_debug_exit(QEMU_TEST_FAIL);
#endif
        cpu_halt_forever();
    }
    serial_write("DAWN-OS: single-CPU kernel-thread scheduler initialized\n");

#ifdef DAWN_KERNEL_TEST
    if (!scheduler_self_test()) {
        serial_write("DAWN-TEST: FAIL: kernel-thread scheduler lifecycle tests\n");
        qemu_debug_exit(QEMU_TEST_FAIL);
        cpu_halt_forever();
    }
    serial_write("DAWN-TEST: PASS: kernel thread creation, interrupt-frame switching, round-robin yield, exit and stack guards\n");
    if (!apic_local_timer_start_calibrated_periodic() ||
        !scheduler_preemption_self_test()) {
        apic_local_timer_stop();
        serial_write("DAWN-TEST: FAIL: timer-driven kernel-thread preemption\n");
        qemu_debug_exit(QEMU_TEST_FAIL);
        cpu_halt_forever();
    }
    apic_local_timer_stop();
    serial_write("DAWN-TEST: PASS: repeated timer-driven kernel-thread preemption\n");
    if (!scheduler_guard_and_state_self_test()) {
        serial_write("DAWN-TEST: FAIL: thread stack guard detection, state machine transitions and cleanup\n");
        qemu_debug_exit(QEMU_TEST_FAIL);
        cpu_halt_forever();
    }
    serial_write("DAWN-TEST: PASS: thread stack guard detection, state machine transitions and cleanup\n");
    if (!scheduler_stress_self_test()) {
        serial_write("DAWN-TEST: FAIL: scheduler heavy preemption stress, non-yielding workers and timer churn\n");
        qemu_debug_exit(QEMU_TEST_FAIL);
        cpu_halt_forever();
    }
    serial_write("DAWN-TEST: PASS: scheduler heavy preemption stress, non-yielding workers and timer churn\n");
    if (!scheduler_process_context_self_test()) {
        serial_write("DAWN-TEST: FAIL: process-bound scheduler context switching\n");
        qemu_debug_exit(QEMU_TEST_FAIL);
        cpu_halt_forever();
    }
    serial_write("DAWN-TEST: PASS: process-bound kernel-thread CR3/TSS switching, yield, exit and reaping\n");
    if (!scheduler_user_context_self_test()) {
        serial_write("DAWN-TEST: FAIL: scheduler-managed CPL3 initial context\n");
        qemu_debug_exit(QEMU_TEST_FAIL);
        cpu_halt_forever();
    }
    serial_write("DAWN-TEST: PASS: process-owned user IRETQ frame, code/stack permissions and trap-stack bounds\n");
    if (!scheduler_elf_launch_self_test()) {
        serial_write("DAWN-TEST: FAIL: controlled CPL3 ELF64 launch and SYS_EXIT return\n");
        qemu_debug_exit(QEMU_TEST_FAIL);
        cpu_halt_forever();
    }
    serial_write("DAWN-TEST: PASS: controlled CPL3 ELF64 launch, syscall exit, scheduler return and process reaping\n");
    if (!scheduler_wait_self_test()) {
        serial_write("DAWN-TEST: FAIL: parent child wait and collection\n");
        qemu_debug_exit(QEMU_TEST_FAIL);
        cpu_halt_forever();
    }
    serial_write("DAWN-TEST: PASS: parent child wait, blocking and collection\n");
    if (!process_spawn_self_test()) {
        serial_write("DAWN-TEST: FAIL: process spawning from in-memory ELF image\n");
        qemu_debug_exit(QEMU_TEST_FAIL);
        cpu_halt_forever();
    }
    serial_write("DAWN-TEST: PASS: process spawning from in-memory ELF image and thread binding\n");
    if (!apic_local_timer_periodic_self_test()) {
        serial_write("DAWN-TEST: FAIL: Local APIC periodic stop/restart checks\n");
        qemu_debug_exit(QEMU_TEST_FAIL);
        cpu_halt_forever();
    }
    serial_write("DAWN-TEST: PASS: Local APIC periodic stop/restart checks\n");
    if (!apic_local_timer_api_self_test()) {
        serial_write("DAWN-TEST: FAIL: Local APIC timer invalid-argument checks\n");
        qemu_debug_exit(QEMU_TEST_FAIL);
        cpu_halt_forever();
    }
    serial_write("DAWN-TEST: PASS: Local APIC timer invalid-argument checks\n");

    /* Phase D: Virtual Filesystem and Filesystem Drivers */
    if (!vfs_self_test()) {
        serial_write("DAWN-TEST: FAIL: VFS mount, hierarchy, path resolution and operations\n");
        qemu_debug_exit(QEMU_TEST_FAIL);
        cpu_halt_forever();
    }
    serial_write("DAWN-TEST: PASS: VFS mount, hierarchy, path resolution and operations\n");
    if (!fat32_self_test()) {
        serial_write("DAWN-TEST: FAIL: FAT32 read and write\n");
        qemu_debug_exit(QEMU_TEST_FAIL);
        cpu_halt_forever();
    }
    serial_write("DAWN-TEST: PASS: FAT32 filesystem parsing, read and write\n");
    if (!iso9660_self_test()) {
        serial_write("DAWN-TEST: FAIL: ISO9660 read\n");
        qemu_debug_exit(QEMU_TEST_FAIL);
        cpu_halt_forever();
    }
    serial_write("DAWN-TEST: PASS: ISO9660 primary volume descriptor and read\n");
    if (!vfs_cache_self_test()) {
        serial_write("DAWN-TEST: FAIL: VFS dentry cache\n");
        qemu_debug_exit(QEMU_TEST_FAIL);
        cpu_halt_forever();
    }
    serial_write("DAWN-TEST: PASS: VFS dentry cache\n");
    if (!explorer_self_test()) {
        serial_write("DAWN-TEST: FAIL: File explorer backend\n");
        qemu_debug_exit(QEMU_TEST_FAIL);
        cpu_halt_forever();
    }
    serial_write("DAWN-TEST: PASS: File explorer backend enumeration, search and copy\n");

    /* Phase E: Storage and Partition Management */
    if (!block_dev_self_test()) {
        serial_write("DAWN-TEST: FAIL: block device registration and read/write\n");
        qemu_debug_exit(QEMU_TEST_FAIL);
        cpu_halt_forever();
    }
    serial_write("DAWN-TEST: PASS: block device registration and read/write\n");
    if (!pci_self_test()) {
        serial_write("DAWN-TEST: FAIL: PCI bus scan and configuration space access\n");
        qemu_debug_exit(QEMU_TEST_FAIL);
        cpu_halt_forever();
    }
    serial_write("DAWN-TEST: PASS: PCI bus scan and configuration space access\n");
    if (!ahci_self_test()) {
        serial_write("DAWN-TEST: FAIL: AHCI SATA controller and port detection\n");
        qemu_debug_exit(QEMU_TEST_FAIL);
        cpu_halt_forever();
    }
    serial_write("DAWN-TEST: PASS: AHCI SATA controller and port detection\n");
    if (!nvme_self_test()) {
        serial_write("DAWN-TEST: FAIL: NVMe controller and namespace queues\n");
        qemu_debug_exit(QEMU_TEST_FAIL);
        cpu_halt_forever();
    }
    serial_write("DAWN-TEST: PASS: NVMe controller and namespace queues\n");
    if (!partition_self_test()) {
        serial_write("DAWN-TEST: FAIL: MBR/GPT partition management and slice translation\n");
        qemu_debug_exit(QEMU_TEST_FAIL);
        cpu_halt_forever();
    }
    serial_write("DAWN-TEST: PASS: MBR/GPT partition management and slice translation\n");
    if (!storage_manager_self_test()) {
        serial_write("DAWN-TEST: FAIL: storage manager disk inventory and operations\n");
        qemu_debug_exit(QEMU_TEST_FAIL);
        cpu_halt_forever();
    }
    serial_write("DAWN-TEST: PASS: storage manager disk inventory and operations\n");

    /* Phase F: Hardware and Device Management */
    if (!device_manager_self_test()) {
        serial_write("DAWN-TEST: FAIL: device manager registry and driver binding\n");
        qemu_debug_exit(QEMU_TEST_FAIL);
        cpu_halt_forever();
    }
    serial_write("DAWN-TEST: PASS: device manager registry and driver binding\n");
    if (!ps2_self_test()) {
        serial_write("DAWN-TEST: FAIL: PS/2 keyboard/mouse scancode decoding\n");
        qemu_debug_exit(QEMU_TEST_FAIL);
        cpu_halt_forever();
    }
    serial_write("DAWN-TEST: PASS: PS/2 keyboard/mouse scancode decoding\n");
    if (!xhci_self_test()) {
        serial_write("DAWN-TEST: FAIL: USB xHCI controller registers and TRB parsing\n");
        qemu_debug_exit(QEMU_TEST_FAIL);
        cpu_halt_forever();
    }
    serial_write("DAWN-TEST: PASS: USB xHCI controller registers and TRB parsing\n");
    if (!usb_hid_self_test()) {
        serial_write("DAWN-TEST: FAIL: USB HID keyboard/mouse report parsing\n");
        qemu_debug_exit(QEMU_TEST_FAIL);
        cpu_halt_forever();
    }
    serial_write("DAWN-TEST: PASS: USB HID keyboard/mouse report parsing\n");
    if (!input_self_test()) {
        serial_write("DAWN-TEST: FAIL: unified input event queue and dispatch\n");
        qemu_debug_exit(QEMU_TEST_FAIL);
        cpu_halt_forever();
    }
    serial_write("DAWN-TEST: PASS: unified input event queue and dispatch\n");
    if (!rtc_self_test()) {
        serial_write("DAWN-TEST: FAIL: real-time clock BCD decoding and formatting\n");
        qemu_debug_exit(QEMU_TEST_FAIL);
        cpu_halt_forever();
    }
    serial_write("DAWN-TEST: PASS: real-time clock BCD decoding and formatting\n");

    /* Phase G: Graphics and Desktop Environment */
    if (!gfx_self_test()) {
        serial_write("DAWN-TEST: FAIL: graphics primitives and font rendering\n");
        qemu_debug_exit(QEMU_TEST_FAIL);
        cpu_halt_forever();
    }
    serial_write("DAWN-TEST: PASS: graphics primitives, alpha blending and font rendering\n");
    if (!wm_self_test()) {
        serial_write("DAWN-TEST: FAIL: window manager lifecycle and controls\n");
        qemu_debug_exit(QEMU_TEST_FAIL);
        cpu_halt_forever();
    }
    serial_write("DAWN-TEST: PASS: window manager lifecycle, Z-order and controls\n");
    if (!desktop_self_test()) {
        serial_write("DAWN-TEST: FAIL: desktop taskbar, launcher and composition\n");
        qemu_debug_exit(QEMU_TEST_FAIL);
        cpu_halt_forever();
    }
    serial_write("DAWN-TEST: PASS: desktop taskbar, launcher and composition\n");
    if (!calculator_self_test()) {
        serial_write("DAWN-TEST: FAIL: calculator application operations\n");
        qemu_debug_exit(QEMU_TEST_FAIL);
        cpu_halt_forever();
    }
    serial_write("DAWN-TEST: PASS: calculator application operations and button layout\n");
    if (!calendar_self_test()) {
        serial_write("DAWN-TEST: FAIL: calendar date calculations and month navigation\n");
        qemu_debug_exit(QEMU_TEST_FAIL);
        cpu_halt_forever();
    }
    serial_write("DAWN-TEST: PASS: calendar date calculations and month navigation\n");

    /* Phase H: Terminal and Shell */
    if (!shell_self_test()) {
        serial_write("DAWN-TEST: FAIL: shell command parser and built-ins\n");
        qemu_debug_exit(QEMU_TEST_FAIL);
        cpu_halt_forever();
    }
    serial_write("DAWN-TEST: PASS: shell command parser, built-in commands and history\n");
    if (!terminal_self_test()) {
        serial_write("DAWN-TEST: FAIL: terminal emulator text grid and scrolling\n");
        qemu_debug_exit(QEMU_TEST_FAIL);
        cpu_halt_forever();
    }
    serial_write("DAWN-TEST: PASS: terminal emulator text grid, scrolling and render buffer\n");

    /* Phase I: Task Manager */
    if (!task_manager_self_test()) {
        serial_write("DAWN-TEST: FAIL: task manager process list and performance monitoring\n");
        qemu_debug_exit(QEMU_TEST_FAIL);
        cpu_halt_forever();
    }
    serial_write("DAWN-TEST: PASS: task manager process list, performance graphs and controls\n");

    /* Phase J: Networking */
    if (!net_self_test()) {
        serial_write("DAWN-TEST: FAIL: network stack, loopback and socket self-test\n");
        qemu_debug_exit(QEMU_TEST_FAIL);
        cpu_halt_forever();
    }
    serial_write("DAWN-TEST: PASS: network checksum, loopback, UDP packet dispatch and socket lifecycle\n");

    /* VirtIO Net PCI & ARP/ICMP Echo Test */
    if (!virtio_net_self_test()) {
        serial_write("DAWN-TEST: FAIL: virtio-net PCI driver, ARP resolution and ICMP echo ping\n");
        qemu_debug_exit(QEMU_TEST_FAIL);
        cpu_halt_forever();
    }
    serial_write("DAWN-TEST: PASS: virtio-net PCI driver, ARP resolution and ICMP echo ping\n");

    /* Phase K: Audio */
    if (!audio_self_test()) {
        serial_write("DAWN-TEST: FAIL: audio mixer, volume control and stream mixing\n");
        qemu_debug_exit(QEMU_TEST_FAIL);
        cpu_halt_forever();
    }
    serial_write("DAWN-TEST: PASS: audio mixer, master volume, stream mixing and tone generation\n");

    /* Phase L: System Settings and Configuration */
    if (!settings_self_test()) {
        serial_write("DAWN-TEST: FAIL: settings serialization, defaults, clamping and window\n");
        qemu_debug_exit(QEMU_TEST_FAIL);
        cpu_halt_forever();
    }
    serial_write("DAWN-TEST: PASS: settings serialization, defaults, clamping and window navigation\n");

    if (!os_metadata_self_test()) {
        serial_write("DAWN-TEST: FAIL: metadata validation, URL handling and safe text\n");
        qemu_debug_exit(QEMU_TEST_FAIL);
        cpu_halt_forever();
    }
    serial_write("DAWN-TEST: PASS: metadata validation, URL handling and safe text\n");

    if (!system_info_self_test()) {
        serial_write("DAWN-TEST: FAIL: system information from kernel APIs\n");
        qemu_debug_exit(QEMU_TEST_FAIL);
        cpu_halt_forever();
    }
    serial_write("DAWN-TEST: PASS: system information from kernel APIs\n");

    /* Phase M: Installer, Recovery and Distribution */
    if (!installer_self_test()) {
        serial_write("DAWN-TEST: FAIL: installer workflow, partition deployment and rescue shell\n");
        qemu_debug_exit(QEMU_TEST_FAIL);
        cpu_halt_forever();
    }
    serial_write("DAWN-TEST: PASS: installer disk verification, partition formatting, file copy and rescue shell\n");

    /* Phase N: Security and User Management */
    if (!security_self_test()) {
        serial_write("DAWN-TEST: FAIL: security password hashing, user authentication and permissions\n");
        qemu_debug_exit(QEMU_TEST_FAIL);
        cpu_halt_forever();
    }
    serial_write("DAWN-TEST: PASS: security password hashing, user authentication, permissions and address boundaries\n");

    /* Phase O: Application Runtime and Software Ecosystem */
    if (!app_runtime_self_test()) {
        serial_write("DAWN-TEST: FAIL: application runtime dlibc, heap pool and package manager\n");
        qemu_debug_exit(QEMU_TEST_FAIL);
        cpu_halt_forever();
    }
    serial_write("DAWN-TEST: PASS: dlibc strings, memory ops, heap allocator, package registry and app lifecycle\n");

    /* Phase P: System Services and Background Infrastructure */
    if (!service_manager_self_test()) {
        serial_write("DAWN-TEST: FAIL: service manager dependencies, IPC bus and event pub/sub\n");
        qemu_debug_exit(QEMU_TEST_FAIL);
        cpu_halt_forever();
    }
    serial_write("DAWN-TEST: PASS: service manager dependencies, daemon lifecycle, IPC queue and event pub/sub\n");

    /* Phase Q: Power, Reliability and Hardware Compatibility */
    if (!power_self_test()) {
        serial_write("DAWN-TEST: FAIL: power management crash report and reliability\n");
        qemu_debug_exit(QEMU_TEST_FAIL);
        cpu_halt_forever();
    }
    serial_write("DAWN-TEST: PASS: ACPI power states, reboot vectors and kernel panic diagnostics\n");

    /* Phase R: Accessibility, Localization and User Experience */
    if (!localization_self_test()) {
        serial_write("DAWN-TEST: FAIL: localization multi-language, keymaps and accessibility\n");
        qemu_debug_exit(QEMU_TEST_FAIL);
        cpu_halt_forever();
    }
    serial_write("DAWN-TEST: PASS: multi-language catalog, keymap translation tables and accessibility scaling\n");

    /* Phase S: Advanced OS Features */
    if (!advanced_self_test()) {
        serial_write("DAWN-TEST: FAIL: advanced spinlocks, semaphores, shm and modules\n");
        qemu_debug_exit(QEMU_TEST_FAIL);
        cpu_halt_forever();
    }
    serial_write("DAWN-TEST: PASS: spinlocks, ticket locks, semaphores, rwlocks, shm, mmap and kernel modules\n");
#endif

    if (framebuffer_response_valid()) {
        serial_write("DAWN-OS: framebuffer request fulfilled\n");
    } else {
        serial_write("DAWN-OS: framebuffer unavailable (optional)\n");
    }

#ifdef DAWN_KERNEL_TEST
    if (!framebuffer_response_valid()) {
        serial_write("DAWN-TEST: FAIL: framebuffer response missing\n");
        qemu_debug_exit(QEMU_TEST_FAIL);
        cpu_halt_forever();
    }

    {
        uint8_t init_image[8192]={0U};
        size_t init_size=0U;
        uint64_t init_pid=0ULL;
        const char *init_args[]={"init",NULL};
        serial_write("DAWN-OS: spawning native init process (PID 1)...\n");
        if (elf64_build_init_image(init_image,sizeof(init_image),&init_size) &&
            process_spawn(init_image,init_size,1,init_args,NULL,&init_pid)) {
            serial_write("DAWN-TEST: PASS: init process spawning and user stack setup\n");
            (void)process_mark_exited(init_pid);
            (void)process_reap(init_pid);
        } else {
            serial_write("DAWN-TEST: FAIL: init process spawning\n");
            qemu_debug_exit(QEMU_TEST_FAIL);
            cpu_halt_forever();
        }
    }

    serial_write("DAWN-TEST: PASS: Limine revision, 4-level paging, memory map, HHDM, allocator, VM manager, IDT, ACPI MADT, xAPIC, IOAPIC, PIT IRQ, LAPIC timer, preemptive scheduler and framebuffer\n");
    serial_write("DAWN-TEST: RESULT=PASS\n");
    qemu_debug_exit(QEMU_TEST_PASS);
#endif

#ifndef DAWN_KERNEL_TEST
    {
        uint8_t init_image[8192]={0U};
        size_t init_size=0U;
        uint64_t init_pid=0ULL;
        const char *init_args[]={"init",NULL};
        serial_write("DAWN-OS: launching native init process (PID 1)...\n");
        if (elf64_build_init_image(init_image,sizeof(init_image),&init_size) &&
            process_spawn(init_image,init_size,1,init_args,NULL,&init_pid)) {
            serial_write("DAWN-OS: Init process (PID 1) running under scheduler\n");
            scheduler_run_until_idle();
        }
    }

    serial_write("DAWN-OS: Phase C process management, file descriptors, wait/exec and init process complete\n");

    /* Start production periodic LAPIC timer for preemption, uptime, and system clock */
    (void)apic_local_timer_start_calibrated_periodic();

    /* Initialize Desktop and Compositor environment */
    (void)desktop_initialize();
    struct gfx_surface *fb = gfx_get_framebuffer();
    serial_write("DAWN-OS: Entering production desktop event and scheduler loop\n");

    while (1) {
        input_poll_hardware();
        struct input_event ev;
        while (input_poll_event(&ev)) {
            desktop_handle_input_event(&ev);
        }
        if (fb != NULL) {
            desktop_render(fb);
        }
        scheduler_run_until_idle();
    }
#endif
    cpu_halt_forever();
}
