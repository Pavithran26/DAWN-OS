/* SPDX-License-Identifier: Unlicense */
#include "dawn.h"

int main(int argc, char *argv[], char *envp[]) {
    (void)envp;
    for (int i = 1; i < argc; i++) {
        if (dawn_strcmp(argv[i], "--help") == 0) {
            dawn_printf("Usage: whoami\n"
                        "Print the user name associated with the current effective user ID.\n");
            return 0;
        }
    }

    dawn_println("root");
    return 0;
}
