/* SPDX-License-Identifier: Unlicense */
#include "dawn.h"

#define MAX_TAIL_LINES 64
#define LINE_MAX_LEN 256

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
    if (max_lines > MAX_TAIL_LINES) max_lines = MAX_TAIL_LINES;

    int64_t fd = 0;
    if (path != NULL) {
        fd = dawn_open(path, DAWN_O_RDONLY, 0);
        if (fd < 0) {
            dawn_printf("tail: cannot open '%s'\n", path);
            return 1;
        }
    }

    /* Circular ring buffer of lines */
    static char ring[MAX_TAIL_LINES][LINE_MAX_LEN];
    int total_lines = 0;
    int cur_col = 0;

    char ch = 0;
    while (dawn_read((uint64_t)fd, &ch, 1) > 0) {
        int idx = total_lines % max_lines;
        if (ch == '\n') {
            ring[idx][cur_col] = '\0';
            total_lines++;
            cur_col = 0;
        } else {
            if (cur_col + 1 < LINE_MAX_LEN) {
                ring[idx][cur_col++] = ch;
            }
        }
    }

    if (cur_col > 0) {
        int idx = total_lines % max_lines;
        ring[idx][cur_col] = '\0';
        total_lines++;
    }

    int print_count = (total_lines < max_lines) ? total_lines : max_lines;
    int start = (total_lines >= max_lines) ? (total_lines - max_lines) : 0;
    for (int i = 0; i < print_count; i++) {
        int idx = (start + i) % max_lines;
        dawn_println(ring[idx]);
    }

    if (path != NULL) {
        dawn_close((uint64_t)fd);
    }
    return 0;
}
