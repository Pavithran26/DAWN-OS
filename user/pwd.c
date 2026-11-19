/* SPDX-License-Identifier: Unlicense */
#include "dawn.h"

int main(int argc, char *argv[], char *envp[]) {
    (void)argc;
    (void)argv;
    (void)envp;
    char cwd[256];
    if (dawn_getcwd(cwd, sizeof(cwd)) >= 0) {
        dawn_printf("%s\n", cwd);
        return 0;
    }
    dawn_printf("/\n");
    return 0;
}
