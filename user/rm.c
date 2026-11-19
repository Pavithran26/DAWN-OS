/* SPDX-License-Identifier: Unlicense */
#include "dawn.h"

int main(int argc, char *argv[], char *envp[]) {
    (void)envp;
    if (argc < 2) {
        dawn_printf("usage: rm <file>\n");
        return 1;
    }

    for (int i = 1; i < argc; i++) {
        int64_t res = dawn_unlink(argv[i]);
        if (res < 0) {
            dawn_printf("rm: cannot remove '%s'\n", argv[i]);
            return 1;
        }
    }
    return 0;
}
