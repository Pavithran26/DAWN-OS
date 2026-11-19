/* SPDX-License-Identifier: Unlicense */
#include "dawn.h"

int main(int argc, char *argv[], char *envp[]) {
    (void)envp;
    char target_dir[128];
    if (argc >= 2) {
        dawn_strcpy(target_dir, argv[1]);
    } else {
        if (dawn_getcwd(target_dir, sizeof(target_dir)) < 0) {
            dawn_strcpy(target_dir, "/");
        }
    }

    int64_t fd = dawn_open(target_dir, DAWN_O_RDONLY, 0);
    if (fd < 0) {
        dawn_printf("ls: cannot access '%s'\n", target_dir);
        return 1;
    }

    /* Print common directory header */
    dawn_printf("Directory listing: %s\n", target_dir);
    dawn_close((uint64_t)fd);
    return 0;
}
