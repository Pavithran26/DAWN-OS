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
    uint64_t timer_preemptions;
    uint64_t active_threads;
    uint64_t user_exit_syscalls;
    uint64_t user_yield_syscalls;
    uint64_t user_wait_syscalls;
    uint64_t user_wait_blocks;
};
bool scheduler_initialize(void);
bool scheduler_is_initialized(void);
bool scheduler_discard_process_threads(uint64_t process_id);
bool kernel_thread_create(kernel_thread_entry entry, void *argument,
                          uint64_t *thread_id);
bool kernel_thread_create_user(uint64_t *thread_id);
/* Bind one READY process to a not-yet-running kernel thread. */
bool kernel_thread_bind_process(uint64_t thread_id, uint64_t process_id);
/* Configure a bound thread's first IRETQ frame to enter a mapped CPL3 context. */
bool kernel_thread_configure_user(uint64_t thread_id,uint64_t process_id,
                                   uint64_t entry,uint64_t user_stack_top);
bool kernel_thread_configure_user_args(uint64_t thread_id,uint64_t process_id,
                                       uint64_t entry,uint64_t user_stack_top,
                                       uint64_t rdi,uint64_t rsi,uint64_t rdx);
bool scheduler_run_until_idle(void);
void kernel_thread_yield(void);
_Noreturn void kernel_thread_exit(void);
void scheduler_get_stats(struct scheduler_stats *stats);
struct interrupt_frame *scheduler_handle_interrupt(
    struct interrupt_frame *frame, bool timer_interrupt);
/* Terminate the current process-bound user thread from a CPL3 trap frame. */
struct interrupt_frame *scheduler_user_exit(struct interrupt_frame *frame);
/* Cooperatively yield a validated CPL3 syscall frame to another ready task. */
struct interrupt_frame *scheduler_user_yield(struct interrupt_frame *frame);
/* Wait for a child process to exit; blocks until child exits if none has exited. */
struct interrupt_frame *scheduler_user_wait(struct interrupt_frame *frame);
/* Spawn a child process or replace image from CPL3 syscall frame. */
struct interrupt_frame *scheduler_user_spawn(struct interrupt_frame *frame);
struct interrupt_frame *scheduler_user_exec(struct interrupt_frame *frame);
bool scheduler_cleanup_all_threads(void);
#ifdef DAWN_KERNEL_TEST
bool scheduler_self_test(void);
bool scheduler_preemption_self_test(void);
bool scheduler_process_context_self_test(void);
bool scheduler_user_context_self_test(void);
bool scheduler_elf_launch_self_test(void);
bool scheduler_wait_self_test(void);
bool scheduler_guard_and_state_self_test(void);
bool scheduler_stress_self_test(void);
#endif
#endif
