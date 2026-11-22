/* SPDX-License-Identifier: Unlicense */
#include "dawn.h"

int main(int argc, char *argv[], char *envp[]) {
    (void)envp;
    bool human = false;
    bool mega = false;

    for (int i = 1; i < argc; i++) {
        if (dawn_strcmp(argv[i], "-h") == 0 || dawn_strcmp(argv[i], "--human") == 0) {
            human = true;
        } else if (dawn_strcmp(argv[i], "-m") == 0 || dawn_strcmp(argv[i], "--mega") == 0) {
            mega = true;
        } else if (dawn_strcmp(argv[i], "--help") == 0) {
            dawn_printf("Usage: free [OPTION]...\n"
                        "Display amount of free and used memory in the system.\n\n"
                        "  -h, --human    show human-readable output (MB/GB)\n"
                        "  -m, --mega     show output in megabytes\n"
                        "      --help     display this help and exit\n");
            return 0;
        } else {
            dawn_printf("free: unrecognized option '%s'\n", argv[i]);
            return 1;
        }
    }

    struct dawn_sysinfo info;
    if (dawn_sysinfo(&info) != 0) {
        dawn_println("free: failed to query system memory");
        return 1;
    }

    uint64_t total = info.total_memory_bytes;
    uint64_t free_mem = info.free_memory_bytes;
    uint64_t used = (total >= free_mem) ? (total - free_mem) : 0ULL;

    dawn_println("               total        used        free");
    if (mega) {
        dawn_printf("Mem:     %8u MB %8u MB %8u MB\n",
                    (unsigned int)(total / (1024ULL * 1024ULL)),
                    (unsigned int)(used / (1024ULL * 1024ULL)),
                    (unsigned int)(free_mem / (1024ULL * 1024ULL)));
    } else if (human) {
        if (total >= 1024ULL * 1024ULL * 1024ULL) {
            dawn_printf("Mem:     %8u GB %8u MB %8u MB\n",
                        (unsigned int)(total / (1024ULL * 1024ULL * 1024ULL)),
                        (unsigned int)(used / (1024ULL * 1024ULL)),
                        (unsigned int)(free_mem / (1024ULL * 1024ULL)));
        } else {
            dawn_printf("Mem:     %8u MB %8u MB %8u MB\n",
                        (unsigned int)(total / (1024ULL * 1024ULL)),
                        (unsigned int)(used / (1024ULL * 1024ULL)),
                        (unsigned int)(free_mem / (1024ULL * 1024ULL)));
        }
    } else {
        dawn_printf("Mem:     %8u KB %8u KB %8u KB\n",
                    (unsigned int)(total / 1024ULL),
                    (unsigned int)(used / 1024ULL),
                    (unsigned int)(free_mem / 1024ULL));
    }

    return 0;
}
