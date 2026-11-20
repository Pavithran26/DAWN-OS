/* SPDX-License-Identifier: Unlicense */
#include "dawn.h"

int main(int argc, char *argv[], char *envp[]) {
    (void)envp;
    bool show_all = false;
    bool show_kernel = false;
    bool show_release = false;
    bool show_arch = false;

    if (argc == 1) {
        show_kernel = true;
    } else {
        for (int i = 1; i < argc; i++) {
            if (dawn_strcmp(argv[i], "-a") == 0 || dawn_strcmp(argv[i], "--all") == 0) {
                show_all = true;
            } else if (dawn_strcmp(argv[i], "-s") == 0 || dawn_strcmp(argv[i], "--kernel-name") == 0) {
                show_kernel = true;
            } else if (dawn_strcmp(argv[i], "-r") == 0 || dawn_strcmp(argv[i], "--kernel-release") == 0) {
                show_release = true;
            } else if (dawn_strcmp(argv[i], "-m") == 0 || dawn_strcmp(argv[i], "--machine") == 0) {
                show_arch = true;
            } else {
                dawn_printf("uname: extra operand '%s'\n", argv[i]);
                return 1;
            }
        }
    }

    if (show_all) {
        dawn_println("DAWN-OS dawn-kernel 1.0.0-emerald x86_64 GNU/DAWN");
    } else {
        bool first = true;
        if (show_kernel) {
            dawn_print("DAWN-OS");
            first = false;
        }
        if (show_release) {
            if (!first) dawn_putchar(' ');
            dawn_print("1.0.0-emerald");
            first = false;
        }
        if (show_arch) {
            if (!first) dawn_putchar(' ');
            dawn_print("x86_64");
        }
        dawn_putchar('\n');
    }

    return 0;
}
