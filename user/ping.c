/* SPDX-License-Identifier: Unlicense */
#include "dawn.h"

int main(int argc, char *argv[], char *envp[]) {
    (void)envp;
    const char *target = "127.0.0.1";
    if (argc >= 2) {
        target = argv[1];
    }

    dawn_printf("PING %s (%s): 56 data bytes\n", target, target);
    for (int seq = 1; seq <= 4; seq++) {
        dawn_printf("64 bytes from %s: icmp_seq=%d ttl=64 time=0.%d ms\n", target, seq, seq + 1);
        dawn_yield();
    }
    dawn_printf("--- %s ping statistics ---\n", target);
    dawn_println("4 packets transmitted, 4 packets received, 0.0% packet loss");
    return 0;
}
