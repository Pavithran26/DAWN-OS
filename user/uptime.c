/* SPDX-License-Identifier: Unlicense */
#include "dawn.h"

int main(int argc, char *argv[], char *envp[]) {
    (void)argc;
    (void)argv;
    (void)envp;

    struct dawn_sysinfo info;
    if (dawn_sysinfo(&info) == 0) {
        uint64_t sec = info.uptime_seconds;
        uint32_t days = (uint32_t)(sec / 86400ULL);
        uint32_t hours = (uint32_t)((sec % 86400ULL) / 3600ULL);
        uint32_t mins = (uint32_t)((sec % 3600ULL) / 60ULL);
        uint32_t secs = (uint32_t)(sec % 60ULL);

        dawn_printf("DAWN-OS Uptime Report\n");
        if (days > 0) {
            dawn_printf("Uptime: %u day%s, %u:%02u:%02u (Online CPUs: %u, Boot: %s)\n",
                        days, days == 1 ? "" : "s", hours, mins, secs,
                        info.online_cpu_count, info.boot_mode);
        } else if (hours > 0) {
            dawn_printf("Uptime: %u:%02u:%02u (Online CPUs: %u, Boot: %s)\n",
                        hours, mins, secs, info.online_cpu_count, info.boot_mode);
        } else {
            dawn_printf("Uptime: %u min, %u sec (Online CPUs: %u, Boot: %s)\n",
                        mins, secs, info.online_cpu_count, info.boot_mode);
        }
    } else {
        uint64_t epoch = 0;
        dawn_time(&epoch);
        dawn_printf("System time: %u seconds since epoch\n", (unsigned int)epoch);
    }

    return 0;
}
