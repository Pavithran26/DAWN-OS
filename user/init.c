/* SPDX-License-Identifier: Unlicense */
#include "dawn.h"

int main(int argc, char *argv[], char *envp[]) {
    (void)argc;
    (void)argv;
    (void)envp;

    dawn_println("============================================================");
    dawn_println(" [DAWN-OS] Native x86_64 Operating System");
    dawn_println(" Emerald Green Neo-Liquid Glass Architecture");
    dawn_println(" Init Process (PID 1) started successfully in Ring 3.");
    dawn_println("============================================================");

    uint64_t pid = dawn_getpid();
    dawn_printf("DAWN-INIT: supervisor PID: %u\n", (unsigned int)pid);

    /* Allocate dynamic memory to verify dawn_malloc */
    char *greeting = (char *)dawn_malloc(64);
    if (greeting != NULL) {
        dawn_strcpy(greeting, "DAWN-INIT: heap memory allocation (sys_brk/malloc) verified.");
        dawn_println(greeting);
        dawn_free(greeting);
    }

    /* Print current working directory */
    char cwd[128];
    if (dawn_getcwd(cwd, sizeof(cwd)) >= 0) {
        dawn_printf("DAWN-INIT: current working directory: %s\n", cwd);
    }

    /* Supervise and reap background children */
    for (int cycle = 0; cycle < 100; cycle++) {
        int64_t status = 0;
        int64_t child = dawn_wait(-1, &status);
        if (child > 0) {
            dawn_printf("DAWN-INIT: collected child PID %d with exit_status=%d\n", (int)child, (int)status);
        } else {
            dawn_yield();
        }
    }

    dawn_println("DAWN-INIT: supervisor running in steady state.");
    return 0;
}
