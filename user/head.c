/* SPDX-License-Identifier: Unlicense */
#include "dawn.h"

int main(int argc, char *argv[], char *envp[]) {
    (void)envp;
    int max_lines = 10;
    const char *path = NULL;

    for (int i = 1; i < argc; i++) {
        if (dawn_strcmp(argv[i], "-n") == 0 && i + 1 < argc) {
            max_lines = dawn_atoi(argv[++i]);
            if (max_lines <= 0) max_lines = 10;
        } else if (argv[i][0] == '-' && argv[i][1] >= '0' && argv[i][1] <= '9') {
            max_lines = dawn_atoi(&argv[i][1]);
            if (max_lines <= 0) max_lines = 10;
        } else {
            path = argv[i];
        }
    }

    int64_t fd = 0; /* Stdin default */
    if (path != NULL) {
        fd = dawn_open(path, DAWN_O_RDONLY, 0);
        if (fd < 0) {
            dawn_printf("head: cannot open '%s'\n", path);
            return 1;
        }
    }

    char ch = 0;
    int lines = 0;
    while (lines < max_lines && dawn_read((uint64_t)fd, &ch, 1) > 0) {
        dawn_putchar(ch);
        if (ch == '\n') {
            lines++;
        }
    }

    if (path != NULL) {
        dawn_close((uint64_t)fd);
    }
    return 0;
}
