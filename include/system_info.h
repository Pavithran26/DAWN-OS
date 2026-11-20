/* SPDX-License-Identifier: Unlicense */
#ifndef DAWN_SYSTEM_INFO_H
#define DAWN_SYSTEM_INFO_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define DAWN_SYSINFO_CPU_MODEL_MAX 64U
#define DAWN_SYSINFO_TEXT_MAX 128U

/* A snapshot of values obtained from real kernel/hardware APIs.
 * Any value that cannot be detected is the literal "Not available". */
struct dawn_system_info {
    char cpu_model[DAWN_SYSINFO_CPU_MODEL_MAX];
    uint32_t cpu_count;
    uint32_t online_cpu_count;
    uint64_t physical_memory_bytes;
    uint64_t free_memory_bytes;
    uint64_t timer_ticks;
    uint32_t timer_hz;
    uint32_t uptime_seconds;
    const char *boot_mode;
    size_t disk_count;
    size_t partition_count;
    size_t mounted_count;
    char storage_summary[DAWN_SYSINFO_TEXT_MAX];
    char filesystem_summary[DAWN_SYSINFO_TEXT_MAX];
};

bool dawn_system_info_refresh(struct dawn_system_info *out);
size_t dawn_decimal_string(uint64_t value, char *dst, size_t capacity);
size_t dawn_human_bytes(uint64_t bytes, char *dst, size_t capacity);

bool system_info_self_test(void);

#endif /* DAWN_SYSTEM_INFO_H */
