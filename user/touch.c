/* SPDX-License-Identifier: Unlicense */
#include "dawn.h"

int main(int argc, char *argv[], char *envp[]) {
    (void)envp;
    if (argc < 2) {
        dawn_println("Usage: touch <file> [file2 ...]");
        return 1;
    }

    int ret = 0;
    for (int i = 1; i < argc; i++) {
        int64_t fd = dawn_open(argv[i], DAWN_O_WRONLY | DAWN_O_CREAT, 0644);
        if (fd < 0) {
            dawn_printf("touch: cannot touch '%s'\n", argv[i]);
            ret = 1;
        } else {
            dawn_close((uint64_t)fd);
        }
    }

    return ret;
}
