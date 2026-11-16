/* SPDX-License-Identifier: Unlicense */
#ifndef DAWN_SCHEDULER_H
#define DAWN_SCHEDULER_H
#include <stdbool.h>
#include <stdint.h>
typedef void (*kernel_thread_entry)(void *argument);
#define SCHEDULER_RESCHEDULE_VECTOR 0x42U
struct interrupt_frame;
struct scheduler_stats {
    uint64_t threads_created;
    uint64_t threads_exited;
    uint64_t context_switches;
    uint64_t voluntary_yields;
    uint64_t active_threads;
};
bool scheduler_initialize(void);
bool kernel_thread_create(kernel_thread_entry entry, void *argument,
                          uint64_t *thread_id);
bool scheduler_run_until_idle(void);
void kernel_thread_yield(void);
_Noreturn void kernel_thread_exit(void);
void scheduler_get_stats(struct scheduler_stats *stats);
struct interrupt_frame *scheduler_handle_interrupt(
    struct interrupt_frame *frame, bool timer_interrupt);
#ifdef DAWN_KERNEL_TEST
bool scheduler_self_test(void);
bool scheduler_preemption_self_test(void);
#endif
#endif
