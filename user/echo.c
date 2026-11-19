/* SPDX-License-Identifier: Unlicense */
#include "dawn.h"

int main(int argc, char *argv[], char *envp[]) {
    (void)envp;
    for (int i = 1; i < argc; i++) {
        dawn_print(argv[i]);
        if (i + 1 < argc) dawn_putchar(' ');
    }
    dawn_putchar('\n');
    return 0;
}
