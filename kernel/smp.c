/* SPDX-License-Identifier: Unlicense */
#include "smp.h"
#include "limine_requests.h"

static struct cpu_core smp_cores[SMP_MAX_CPUS];
static uint32_t smp_total_cpus = 1U;
static volatile uint32_t smp_online_count = 0U;
static uint32_t smp_bsp_id = 0U;
static bool smp_initialized = false;

static void smp_ap_entry(struct limine_smp_info *info) {
    if (info == NULL) {
        while (1) __asm__ volatile ("pause; hlt");
    }

    struct cpu_core *core = (struct cpu_core *)(uintptr_t)info->extra_argument;
    if (core == NULL) {
        while (1) __asm__ volatile ("pause; hlt");
    }

    /* Switch to dedicated per-core kernel stack */
    void *stack_top = (void *)(core->stack + SMP_STACK_SIZE);
    __asm__ volatile (
        "mov %0, %%rsp\n\t"
        :
        : "r"(stack_top)
        : "memory"
    );

    core->online = true;
    (void)__sync_fetch_and_add(&smp_online_count, 1U);

    /* AP idle loop */
    while (1) {
        core->heartbeat++;
        __asm__ volatile ("pause; hlt");
    }
}

bool smp_initialize(void) {
    if (smp_initialized) return true;

    for (uint32_t i = 0; i < SMP_MAX_CPUS; i++) {
        smp_cores[i].cpu_index = i;
        smp_cores[i].lapic_id = i;
        smp_cores[i].is_bsp = (i == 0);
        smp_cores[i].online = false;
        smp_cores[i].heartbeat = 0ULL;
    }

    struct limine_smp_response *resp = limine_smp_request.response;
    if (resp == NULL || resp->cpu_count == 0ULL || resp->cpus == NULL) {
        /* Uniprocessor fallback */
        smp_total_cpus = 1U;
        smp_online_count = 1U;
        smp_bsp_id = 0U;
        smp_cores[0].is_bsp = true;
        smp_cores[0].online = true;
        smp_initialized = true;
        return true;
    }

    smp_total_cpus = (uint32_t)resp->cpu_count;
    if (smp_total_cpus > SMP_MAX_CPUS) smp_total_cpus = SMP_MAX_CPUS;
    smp_bsp_id = resp->bsp_lapic_id;

    /* Initialize BSP */
    smp_online_count = 1U; /* BSP is already running */

    for (uint32_t i = 0; i < smp_total_cpus; i++) {
        struct limine_smp_info *info = resp->cpus[i];
        if (info == NULL) continue;

        smp_cores[i].cpu_index = i;
        smp_cores[i].lapic_id = info->lapic_id;

        if (info->lapic_id == smp_bsp_id) {
            smp_cores[i].is_bsp = true;
            smp_cores[i].online = true;
        } else {
            smp_cores[i].is_bsp = false;
            smp_cores[i].online = false;

            /* Start Application Processor */
            info->extra_argument = (uint64_t)(uintptr_t)&smp_cores[i];
            __sync_synchronize();
            info->goto_address = smp_ap_entry;
        }
    }

    /* Wait up to 500000 spin iterations for all APs to boot */
    for (volatile uint32_t spin = 0; spin < 500000U; spin++) {
        if (smp_online_count >= smp_total_cpus) break;
        __asm__ volatile ("pause");
    }

    smp_initialized = true;
    return true;
}

uint32_t smp_cpu_count(void) {
    return smp_total_cpus;
}

uint32_t smp_online_cpu_count(void) {
    return smp_online_count;
}

uint32_t smp_bsp_lapic_id(void) {
    return smp_bsp_id;
}

bool smp_is_bsp(void) {
    return true; /* Kernel main thread executes on BSP */
}

struct cpu_core *smp_get_core(uint32_t index) {
    if (index >= SMP_MAX_CPUS) return NULL;
    return &smp_cores[index];
}

bool smp_self_test(void) {
    if (!smp_initialize()) return false;

    if (smp_total_cpus == 0U) return false;
    if (smp_online_count == 0U) return false;
    if (smp_online_count > smp_total_cpus) return false;

    /* Verify BSP core entry */
    bool found_bsp = false;
    for (uint32_t i = 0; i < smp_total_cpus; i++) {
        if (smp_cores[i].is_bsp) {
            found_bsp = true;
            if (!smp_cores[i].online) return false;
        }
    }
    if (!found_bsp) return false;

    /* Verify get_core API */
    struct cpu_core *c0 = smp_get_core(0);
    if (c0 == NULL) return false;
    if (smp_get_core(SMP_MAX_CPUS + 1U) != NULL) return false;

    return true;
}
