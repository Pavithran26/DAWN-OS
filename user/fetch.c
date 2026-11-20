/* SPDX-License-Identifier: Unlicense */
#include "dawn.h"

int main(int argc, char *argv[], char *envp[]) {
    (void)argc;
    (void)argv;
    (void)envp;

    uint64_t epoch = 0;
    dawn_time(&epoch);

    dawn_println("\033[32m       /\\         \033[1;37muser\033[0m@\033[32mdawn-os\033[0m");
    dawn_println("\033[32m      /  \\        \033[0;37m---------------------\033[0m");
    dawn_println("\033[32m     / /\\ \\       \033[1;32mOS:\033[0m DAWN-OS 1.0.0 (Emerald Green Edition)");
    dawn_println("\033[32m    / /  \\ \\      \033[1;32mKernel:\033[0m x86_64 Freestanding (Limine Boot)");
    dawn_printf ("\033[32m   / / /\\ \\ \\     \033[1;32mUptime:\033[0m %u seconds\n", (unsigned int)epoch);
    dawn_println("\033[32m  / / /__\\ \\ \\    \033[1;32mShell:\033[0m dsh v1.0 (POSIX Ring 3)");
    dawn_println("\033[32m / /________\\ \\   \033[1;32mResolution:\033[0m 1024x768 32bpp");
    dawn_println("\033[32m/______________\\  \033[1;32mUI:\033[0m Neo-Liquid Emerald Glass Compositor\033[0m");
    dawn_println("                  \033[42m   \033[46m   \033[44m   \033[45m   \033[47m   \033[0m\n");
    return 0;
}
