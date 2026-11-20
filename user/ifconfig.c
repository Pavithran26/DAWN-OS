/* SPDX-License-Identifier: Unlicense */
#include "dawn.h"

int main(int argc, char *argv[], char *envp[]) {
    (void)argc;
    (void)argv;
    (void)envp;

    dawn_println("eth0: flags=4163<UP,BROADCAST,RUNNING,MULTICAST>  mtu 1500");
    dawn_println("        inet 10.0.2.15  netmask 255.255.255.0  broadcast 10.0.2.255");
    dawn_println("        gateway 10.0.2.2");
    dawn_println("        ether 52:54:00:12:34:56  txqueuelen 1000  (Ethernet)");
    dawn_println("        RX packets 42  bytes 3864 (3.8 KB)");
    dawn_println("        TX packets 38  bytes 3420 (3.4 KB)\n");

    dawn_println("lo: flags=73<UP,LOOPBACK,RUNNING>  mtu 65536");
    dawn_println("        inet 127.0.0.1  netmask 255.0.0.0");
    dawn_println("        loop  txqueuelen 1000  (Local Loopback)");
    dawn_println("        RX packets 128  bytes 12800 (12.8 KB)");
    dawn_println("        TX packets 128  bytes 12800 (12.8 KB)");

    return 0;
}
