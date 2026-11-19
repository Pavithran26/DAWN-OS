/* SPDX-License-Identifier: Unlicense */
#include "system_info.h"
#include "apic.h"
#include "block_dev.h"
#include "os_metadata.h"
#include "physical_memory.h"
#include "storage_manager.h"
#include "limine_requests.h"

static const char not_available[] = "Not available";

static void local_cpuid(uint32_t leaf, uint32_t *eax, uint32_t *ebx,
                        uint32_t *ecx, uint32_t *edx) {
    __asm__ volatile ("cpuid"
                      : "=a"(*eax), "=b"(*ebx), "=c"(*ecx), "=d"(*edx)
                      : "a"(leaf), "c"(0U));
}

size_t dawn_decimal_string(uint64_t value, char *dst, size_t capacity) {
    char temporary[24];
    size_t count = 0U;
    size_t index = 0U;
    if (dst == NULL || capacity == 0U) return 0U;
    if (value == 0ULL) {
        if (capacity < 2U) return 0U;
        dst[0] = '0';
        dst[1] = '\0';
        return 1U;
    }
    while (value > 0ULL && count < sizeof(temporary)) {
        temporary[count++] = (char)('0' + (unsigned int)(value % 10ULL));
        value /= 10ULL;
    }
    if (count + 1U > capacity) return 0U;
    while (index < count) {
        dst[index] = temporary[count - 1U - index];
        ++index;
    }
    dst[index] = '\0';
    return index;
}

size_t dawn_human_bytes(uint64_t bytes, char *dst, size_t capacity) {
    const char *units[4] = {" B", " KiB", " MiB", " GiB"};
    uint64_t scaled = bytes;
    size_t unit = 0U;
    char number[24];
    size_t number_length = 0U;
    size_t total = 0U;
    if (dst == NULL || capacity == 0U) return 0U;
    while (scaled >= 1024ULL && unit < 3U) {
        scaled /= 1024ULL;
        ++unit;
    }
    number_length = dawn_decimal_string(scaled, number, sizeof(number));
    if (number_length == 0U) return 0U;
    const char *suffix = units[unit];
    size_t suffix_length = 0U;
    while (suffix[suffix_length] != '\0') ++suffix_length;
    if (number_length + suffix_length + 1U > capacity) return 0U;
    for (size_t i = 0U; i < number_length; ++i) dst[total++] = number[i];
    for (size_t i = 0U; i < suffix_length; ++i) dst[total++] = suffix[i];
    dst[total] = '\0';
    return total;
}

static void local_read_cpu_model(char *dst, size_t capacity) {
    uint32_t registers[12] = {0U};
    uint8_t *bytes = (uint8_t *)registers;
    size_t length = 0U;
    uint32_t largest_extended = 0U;
    uint32_t b = 0U, c = 0U, d = 0U;

    if (dst == NULL || capacity == 0U) return;
    local_cpuid(0x80000000U, &largest_extended, &b, &c, &d);
    if (largest_extended < 0x80000004U) {
        dst[0] = '\0';
        return;
    }
    for (uint32_t index = 0U; index < 3U; ++index) {
        uint32_t *slot = &registers[index * 4U];
        local_cpuid(0x80000002U + index, &slot[0], &slot[1], &slot[2], &slot[3]);
    }
    while (length < sizeof(registers) && length + 1U < capacity && bytes[length] != 0U) ++length;
    while (length > 0U && (bytes[length - 1U] == ' ' || bytes[length - 1U] == '\0')) --length;
    for (size_t index = 0U; index < length; ++index) dst[index] = (char)bytes[index];
    dst[length] = '\0';
}

bool dawn_system_info_refresh(struct dawn_system_info *out) {
    size_t disk_count = 0U;
    size_t partition_count = 0U;
    size_t mounted_count = 0U;
    uint32_t frequency = 0U;
    uint64_t ticks = 0ULL;

    if (out == NULL) return false;
    out->physical_memory_bytes = physical_memory_total_bytes();
    out->free_memory_bytes = physical_memory_free_bytes();
    out->timer_hz = apic_local_timer_calibrated_hz();
    out->timer_ticks = apic_local_timer_tick_count();

    frequency = out->timer_hz;
    ticks = out->timer_ticks;
    out->uptime_seconds = (frequency != 0U) ? (uint32_t)(ticks / (uint64_t)frequency) : 0U;

    local_read_cpu_model(out->cpu_model, sizeof(out->cpu_model));
    if (out->cpu_model[0] == '\0') {
        size_t index = 0U;
        while (index + 1U < sizeof(out->cpu_model) && not_available[index] != '\0') {
            out->cpu_model[index] = not_available[index];
            ++index;
        }
        out->cpu_model[index] = '\0';
    }

    /* Detect firmware boot mode through the Limine protocol. */
    if (limine_efi_system_table_request.response != NULL &&
        limine_efi_system_table_request.response->address != NULL) {
        out->boot_mode = "UEFI (Limine)";
    } else if (limine_framebuffer_request.response != NULL ||
               limine_memmap_request.response != NULL) {
        out->boot_mode = "BIOS (Limine)";
    } else {
        out->boot_mode = not_available;
    }

    disk_count = storage_get_disk_count();
    out->disk_count = disk_count;
    for (size_t index = 0U; index < disk_count; ++index) {
        const struct storage_disk_desc *disk = storage_get_disk_info(index);
        if (disk == NULL) continue;
        partition_count += (size_t)disk->partition_count;
        for (uint32_t part = 0U; part < disk->partition_count; ++part) {
            if (disk->partitions[part].mounted) ++mounted_count;
        }
    }
    out->partition_count = partition_count;
    out->mounted_count = mounted_count;

    out->storage_summary[0] = '\0';
    if (disk_count == 0U) {
        size_t index = 0U;
        while (index + 1U < sizeof(out->storage_summary) && not_available[index] != '\0') {
            out->storage_summary[index] = not_available[index];
            ++index;
        }
        out->storage_summary[index] = '\0';
    } else {
        const struct storage_disk_desc *disk = storage_get_disk_info(0U);
        char capacity[24];
        size_t position = 0U;
        size_t length = 0U;
        if (disk != NULL && dawn_human_bytes(disk->capacity_bytes, capacity, sizeof(capacity)) != 0U) {
            const char *prefix = "Disk 0: ";
            while (prefix[position] != '\0' && position + 1U < sizeof(out->storage_summary)) {
                out->storage_summary[position] = prefix[position];
                ++position;
            }
            length = 0U;
            while (capacity[length] != '\0' && position + length + 1U < sizeof(out->storage_summary)) {
                out->storage_summary[position + length] = capacity[length];
                ++length;
            }
            position += length;
            out->storage_summary[position] = '\0';
        } else {
            out->storage_summary[0] = '\0';
        }
    }

    out->filesystem_summary[0] = '\0';
    if (disk_count == 0U || partition_count == 0U) {
        size_t index = 0U;
        while (index + 1U < sizeof(out->filesystem_summary) && not_available[index] != '\0') {
            out->filesystem_summary[index] = not_available[index];
            ++index;
        }
        out->filesystem_summary[index] = '\0';
    } else {
        const struct storage_disk_desc *disk = storage_get_disk_info(0U);
        if (disk != NULL && disk->partition_count > 0U) {
            const char *fs = disk->partitions[0U].fs_name;
            size_t index = 0U;
            while (fs[index] != '\0' && index + 1U < sizeof(out->filesystem_summary)) {
                out->filesystem_summary[index] = fs[index];
                ++index;
            }
            out->filesystem_summary[index] = '\0';
            if (out->filesystem_summary[0] == '\0') {
                out->filesystem_summary[0] = '?';
                out->filesystem_summary[1] = '\0';
            }
        }
    }

    return dawn_metadata_is_valid();
}

bool system_info_self_test(void) {
    struct dawn_system_info info;
    char buffer[32];
    if (!dawn_system_info_refresh(&info)) return false;
    if (info.cpu_model[0] == '\0') return false;
    if (dawn_decimal_string(0ULL, buffer, sizeof(buffer)) != 1U) return false;
    if (buffer[0] != '0' || buffer[1] != '\0') return false;
    if (dawn_decimal_string(1024ULL, buffer, sizeof(buffer)) != 4U) return false;
    if (dawn_human_bytes(0ULL, buffer, sizeof(buffer)) == 0U) return false;
    if (dawn_human_bytes(2048ULL, buffer, sizeof(buffer)) == 0U) return false;
    if (info.boot_mode == NULL) return false;
    return true;
}
