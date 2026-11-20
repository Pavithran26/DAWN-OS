/* SPDX-License-Identifier: Unlicense */
#include "dawn.h"

int main(int argc, char *argv[], char *envp[]) {
    (void)envp;
    if (argc < 3) {
        dawn_println("Usage: cp <source> <destination>");
        return 1;
    }

    const char *src_path = argv[1];
    const char *dst_path = argv[2];

    int64_t src_fd = dawn_open(src_path, DAWN_O_RDONLY, 0);
    if (src_fd < 0) {
        dawn_printf("cp: cannot open '%s' for reading\n", src_path);
        return 1;
    }

    int64_t dst_fd = dawn_open(dst_path, DAWN_O_WRONLY | DAWN_O_CREAT, 0644);
    if (dst_fd < 0) {
        dawn_printf("cp: cannot create '%s'\n", dst_path);
        dawn_close((uint64_t)src_fd);
        return 1;
    }

    char buf[512];
    int64_t bytes = 0;
    while ((bytes = dawn_read((uint64_t)src_fd, buf, sizeof(buf))) > 0) {
        int64_t written = dawn_write((uint64_t)dst_fd, buf, (uint64_t)bytes);
        if (written < bytes) {
            dawn_printf("cp: error writing to '%s'\n", dst_path);
            break;
        }
    }

    dawn_close((uint64_t)src_fd);
    dawn_close((uint64_t)dst_fd);
    return 0;
}
