/* SPDX-License-Identifier: Unlicense */
#include "dawn.h"

int main(int argc, char *argv[], char *envp[]) {
    (void)envp;
    if (argc < 2) {
        dawn_printf("Usage: sleep NUMBER\n"
                    "Pause for NUMBER seconds.\n");
        return 1;
    }

    if (dawn_strcmp(argv[1], "--help") == 0) {
        dawn_printf("Usage: sleep NUMBER\n"
                    "Pause for NUMBER seconds.\n");
        return 0;
    }

    int sec = dawn_atoi(argv[1]);
    if (sec <= 0) return 0;

    uint64_t start = 0;
    dawn_time(&start);
    uint64_t now = start;

    while ((now - start) < (uint64_t)sec) {
        dawn_yield();
        dawn_time(&now);
    }

    return 0;
}
