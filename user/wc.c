/* SPDX-License-Identifier: Unlicense */
#include "dawn.h"

static inline bool is_space(char c) {
    return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f' || c == '\v';
}

int main(int argc, char *argv[], char *envp[]) {
    (void)envp;
    const char *path = NULL;
    if (argc >= 2) {
        path = argv[1];
    }

    int64_t fd = 0;
    if (path != NULL) {
        fd = dawn_open(path, DAWN_O_RDONLY, 0);
        if (fd < 0) {
            dawn_printf("wc: '%s': No such file or directory\n", path);
            return 1;
        }
    }

    uint64_t lines = 0;
    uint64_t words = 0;
    uint64_t bytes = 0;
    bool in_word = false;

    char buf[512];
    int64_t n = 0;
    while ((n = dawn_read((uint64_t)fd, buf, sizeof(buf))) > 0) {
        bytes += (uint64_t)n;
        for (int64_t i = 0; i < n; i++) {
            char c = buf[i];
            if (c == '\n') {
                lines++;
            }
            if (is_space(c)) {
                in_word = false;
            } else if (!in_word) {
                in_word = true;
                words++;
            }
        }
    }

    if (path != NULL) {
        dawn_printf(" %u  %u  %u %s\n", (unsigned int)lines, (unsigned int)words, (unsigned int)bytes, path);
        dawn_close((uint64_t)fd);
    } else {
        dawn_printf(" %u  %u  %u\n", (unsigned int)lines, (unsigned int)words, (unsigned int)bytes);
    }

    return 0;
}
