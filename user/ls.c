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

    struct dawn_dirent dent;
    int count = 0;
    while (dawn_getdents((uint64_t)fd, &dent, sizeof(dent)) > 0) {
        count++;
        if (dent.d_type == 2) { /* Directory */
            dawn_printf("[DIR]  %-20s\n", dent.d_name);
        } else {
            dawn_printf("       %-20s %u B\n", dent.d_name, (unsigned int)dent.d_size);
        }
    }

    dawn_close((uint64_t)fd);
    return 0;
}
