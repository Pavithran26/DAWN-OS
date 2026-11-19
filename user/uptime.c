/* SPDX-License-Identifier: Unlicense */
#include "dawn.h"

int main(int argc, char *argv[], char *envp[]) {
    (void)argc;
    (void)argv;
    (void)envp;
    uint64_t epoch = 0;
    dawn_time(&epoch);

    dawn_printf("DAWN-OS Uptime Report\n");
    dawn_printf("System time: %u seconds since epoch\n", (unsigned int)epoch);
    return 0;
}
