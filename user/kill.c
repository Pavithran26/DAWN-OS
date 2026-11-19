/* SPDX-License-Identifier: Unlicense */
#include "dawn.h"

int main(int argc, char *argv[], char *envp[]) {
    (void)envp;
    if (argc < 2) {
        dawn_printf("usage: kill <pid>\n");
        return 1;
    }

    uint64_t pid = 0;
    const char *s = argv[1];
    while (*s >= '0' && *s <= '9') {
        pid = pid * 10 + (uint64_t)(*s - '0');
        s++;
    }

    if (pid == 0) {
        dawn_printf("kill: invalid PID '%s'\n", argv[1]);
        return 1;
    }

    int64_t res = dawn_kill(pid, 9);
    if (res < 0) {
        dawn_printf("kill: failed to signal process %u\n", (unsigned int)pid);
        return 1;
    }
    return 0;
}
