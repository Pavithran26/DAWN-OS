/* SPDX-License-Identifier: Unlicense */
#include "dawn.h"

int main(int argc, char *argv[], char *envp[]) {
    (void)envp;
    bool all = false;

    for (int i = 1; i < argc; i++) {
        if (dawn_strcmp(argv[i], "--all") == 0) {
            all = true;
        } else if (dawn_strcmp(argv[i], "--help") == 0) {
            dawn_printf("Usage: nproc [OPTION]...\n"
                        "Print the number of processing units available to the current process.\n\n"
                        "      --all      print the number of installed processors\n"
                        "      --help     display this help and exit\n");
            return 0;
        } else {
            dawn_printf("nproc: unrecognized option '%s'\n", argv[i]);
            return 1;
        }
    }

    struct dawn_sysinfo info;
    if (dawn_sysinfo(&info) != 0) {
        dawn_println("1");
        return 0;
    }

    uint32_t cpus = all ? info.cpu_count : info.online_cpu_count;
    if (cpus == 0) cpus = 1;
    dawn_printf("%u\n", cpus);
    return 0;
}
