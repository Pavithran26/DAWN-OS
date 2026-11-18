/* SPDX-License-Identifier: Unlicense */
#ifndef DAWN_POWER_H
#define DAWN_POWER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

enum power_action {
    POWER_ACTION_NONE = 0,
    POWER_ACTION_REBOOT = 1,
    POWER_ACTION_SHUTDOWN = 2,
    POWER_ACTION_SUSPEND = 3
};

struct cpu_crash_context {
    uint64_t rip;
    uint64_t rsp;
    uint64_t rbp;
    uint64_t rax;
    uint64_t rbx;
    uint64_t rcx;
    uint64_t rdx;
    uint64_t rsi;
    uint64_t rdi;
    uint64_t rflags;
    uint64_t cr2;
    uint64_t cr3;
};

/* Core Power Management API */
bool power_initialize(void);
void power_cpu_idle(void);
void power_reboot(void) __attribute__((noreturn));
void power_shutdown(void) __attribute__((noreturn));

/* Reliability & Kernel Panic Diagnostics */
void kernel_panic(const char *reason, const struct cpu_crash_context *ctx) __attribute__((noreturn));
void kernel_panic_simple(const char *reason) __attribute__((noreturn));
bool power_format_crash_report(const char *reason, const struct cpu_crash_context *ctx, char *buf, size_t max_len);

/* Self-test */
bool power_self_test(void);

#endif /* DAWN_POWER_H */
