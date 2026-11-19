/* SPDX-License-Identifier: Unlicense */
#include "dawn.h"

int main(int argc, char *argv[], char *envp[]) {
    (void)envp;
    if (argc < 2) {
        dawn_println("Usage: cat <file>");
        return 1;
    }

    int64_t fd = dawn_open(argv[1], DAWN_O_RDONLY, 0);
    if (fd < 0) {
        dawn_printf("cat: '%s': No such file or directory\n", argv[1]);
        return 1;
    }

    char buf[512];
    int64_t bytes = 0;
    while ((bytes = dawn_read((uint64_t)fd, buf, sizeof(buf) - 1)) > 0) {
        buf[bytes] = '\0';
        dawn_print(buf);
    }
    dawn_putchar('\n');
    dawn_close((uint64_t)fd);
    return 0;
}
