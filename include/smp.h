/* SPDX-License-Identifier: Unlicense */
#ifndef DAWN_SMP_H
#define DAWN_SMP_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define SMP_MAX_CPUS 32U
#define SMP_STACK_SIZE 16384U

struct cpu_core {
    uint32_t cpu_index;
    uint32_t lapic_id;
    bool is_bsp;
    volatile bool online;
    volatile uint64_t heartbeat;
    uint8_t stack[SMP_STACK_SIZE] __attribute__((aligned(16)));
};

bool smp_initialize(void);
uint32_t smp_cpu_count(void);
uint32_t smp_online_cpu_count(void);
uint32_t smp_bsp_lapic_id(void);
bool smp_is_bsp(void);
struct cpu_core *smp_get_core(uint32_t index);

bool smp_self_test(void);

#endif /* DAWN_SMP_H */
