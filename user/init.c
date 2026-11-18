/* SPDX-License-Identifier: Unlicense */
#include "dawn.h"

int main(int argc, char *argv[], char *envp[]) {
    (void)argc;
    (void)argv;
    (void)envp;

    dawn_println("============================================================");
    dawn_println(" [DAWN-OS] Native x86_64 Operating System");
    dawn_println(" Emerald Green Neo-Liquid Glass Architecture");
    dawn_println(" Init Process (PID 1) started successfully.");
    dawn_println("============================================================");

    uint64_t pid = dawn_getpid();
    dawn_print("DAWN-INIT: process identifier: ");
    dawn_print_dec(pid);
    dawn_println("");

    /* Cooperatively yield to scheduler */
    dawn_yield();

    /* Supervise and reap children */
    for (int cycle = 0; cycle < 1000; cycle++) {
        int64_t status = 0;
        int64_t child = dawn_wait(-1, &status);
        if (child > 0) {
            dawn_print("DAWN-INIT: collected child PID ");
            dawn_print_dec((uint64_t)child);
            dawn_print(" exit_status=");
            dawn_print_dec((uint64_t)status);
            dawn_println("");
        } else {
            dawn_yield();
        }
    }

    dawn_println("DAWN-INIT: init supervisor completed initial cycles.");
    return 0;
}
