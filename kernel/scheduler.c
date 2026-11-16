/* SPDX-License-Identifier: Unlicense */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "scheduler.h"

#define KERNEL_THREAD_MAX 8U
#define KERNEL_THREAD_STACK_SIZE (16U * 1024U)
#define KERNEL_THREAD_GUARD_SIZE 64U
#define KERNEL_THREAD_GUARD_BYTE 0xa5U
#define CPU_RFLAGS_INTERRUPT_ENABLE (1ULL << 9)

enum thread_state { THREAD_FREE=0, THREAD_READY, THREAD_RUNNING, THREAD_DEAD };
struct thread_context { uint64_t rsp; };
struct kernel_thread {
    struct thread_context context;
    kernel_thread_entry entry;
    void *argument;
    uint64_t id;
    enum thread_state state;
    uint8_t stack[KERNEL_THREAD_STACK_SIZE] __attribute__((aligned(16)));
};

static struct kernel_thread thread_pool[KERNEL_THREAD_MAX];
static struct kernel_thread bootstrap_thread;
static struct kernel_thread *current_thread;
static uint64_t ready_cursor;
static uint64_t next_thread_id;
static bool initialized;
static bool scheduler_running;
static struct scheduler_stats counters;

extern void arch_context_switch(uint64_t *old_rsp,const uint64_t *new_rsp);
extern void kernel_thread_bootstrap_trampoline(void);

static inline uint64_t read_rflags(void) {
    uint64_t flags=0ULL;
    __asm__ volatile ("pushfq; popq %0" : "=r"(flags));
    return flags;
}
static inline void disable_interrupts(void) {
    __asm__ volatile ("cli" : : : "memory");
}
static inline void restore_interrupt_flag(uint64_t saved_flags) {
    if ((saved_flags & CPU_RFLAGS_INTERRUPT_ENABLE)!=0ULL)
        __asm__ volatile ("sti" : : : "memory");
}
static void cpu_halt_forever(void) {
    disable_interrupts();
    for (;;) __asm__ volatile ("hlt");
}
static void increment_saturated(uint64_t *value) {
    if (value!=NULL && *value!=UINT64_MAX) ++(*value);
}
static void thread_guard_initialize(struct kernel_thread *thread) {
    for (size_t i=0U;i<KERNEL_THREAD_GUARD_SIZE;i++)
        thread->stack[i]=KERNEL_THREAD_GUARD_BYTE;
}
static bool thread_guard_valid(const struct kernel_thread *thread) {
    if (thread==NULL) return false;
    for (size_t i=0U;i<KERNEL_THREAD_GUARD_SIZE;i++)
        if (thread->stack[i]!=KERNEL_THREAD_GUARD_BYTE) return false;
    return true;
}
static void thread_context_initialize(struct kernel_thread *thread) {
    uintptr_t *stack_top=NULL;
    if (thread==NULL) return;
    stack_top=(uintptr_t *)(void *)(thread->stack+KERNEL_THREAD_STACK_SIZE);
    stack_top=(uintptr_t *)((uintptr_t)stack_top & ~(uintptr_t)0xfU);
    /* Restore order: r15,r14,r13,r12,rbp,rbx, then RET to trampoline. */
    *--stack_top=(uintptr_t)kernel_thread_bootstrap_trampoline;
    *--stack_top=0ULL;
    *--stack_top=0ULL;
    *--stack_top=0ULL;
    *--stack_top=0ULL;
    *--stack_top=0ULL;
    *--stack_top=0ULL;
    thread->context.rsp=(uint64_t)(uintptr_t)stack_top;
}
static struct kernel_thread *find_ready_thread(
    const struct kernel_thread *exclude
) {
    for (uint64_t attempt=0ULL;attempt<KERNEL_THREAD_MAX;attempt++) {
        const uint64_t index=(ready_cursor+attempt)%KERNEL_THREAD_MAX;
        struct kernel_thread *candidate=&thread_pool[(size_t)index];
        if (candidate!=exclude && candidate->state==THREAD_READY) {
            ready_cursor=(index+1ULL)%KERNEL_THREAD_MAX;
            return candidate;
        }
    }
    return NULL;
}
static void switch_to_thread(struct kernel_thread *next) {
    struct kernel_thread *previous=current_thread;
    if (previous==NULL || next==NULL || previous==next ||
        (previous!=&bootstrap_thread && !thread_guard_valid(previous)) ||
        (next!=&bootstrap_thread && !thread_guard_valid(next)))
        cpu_halt_forever();
    current_thread=next;
    increment_saturated(&counters.context_switches);
    arch_context_switch(&previous->context.rsp,&next->context.rsp);
}
void kernel_thread_bootstrap(void) {
    struct kernel_thread *thread=current_thread;
    if (thread==NULL || thread==&bootstrap_thread ||
        thread->entry==NULL || thread->state!=THREAD_RUNNING)
        cpu_halt_forever();
    thread->entry(thread->argument);
    kernel_thread_exit();
}
bool scheduler_initialize(void) {
    const uint64_t saved_flags=read_rflags();
    if (initialized) return false;
    disable_interrupts();
    for (size_t i=0U;i<KERNEL_THREAD_MAX;i++) {
        thread_pool[i].context.rsp=0ULL;
        thread_pool[i].entry=NULL;
        thread_pool[i].argument=NULL;
        thread_pool[i].id=0ULL;
        thread_pool[i].state=THREAD_FREE;
    }
    bootstrap_thread.context.rsp=0ULL;
    bootstrap_thread.entry=NULL;
    bootstrap_thread.argument=NULL;
    bootstrap_thread.id=0ULL;
    bootstrap_thread.state=THREAD_RUNNING;
    ready_cursor=0ULL;
    next_thread_id=1ULL;
    scheduler_running=false;
    counters=(struct scheduler_stats){0};
    current_thread=&bootstrap_thread;
    initialized=true;
    restore_interrupt_flag(saved_flags);
    return true;
}
bool kernel_thread_create(kernel_thread_entry entry,void *argument,
                          uint64_t *thread_id) {
    uint64_t saved_flags=0ULL;
    struct kernel_thread *thread=NULL;
    if (!initialized || scheduler_running || entry==NULL ||
        current_thread!=&bootstrap_thread) return false;
    saved_flags=read_rflags();
    disable_interrupts();
    for (size_t i=0U;i<KERNEL_THREAD_MAX;i++) {
        if (thread_pool[i].state==THREAD_FREE ||
            thread_pool[i].state==THREAD_DEAD) {
            thread=&thread_pool[i];
            break;
        }
    }
    if (thread==NULL || next_thread_id==0ULL ||
        next_thread_id==UINT64_MAX) {
        restore_interrupt_flag(saved_flags);
        return false;
    }
    if (thread->state==THREAD_DEAD && !thread_guard_valid(thread)) {
        restore_interrupt_flag(saved_flags);
        return false;
    }
    thread_guard_initialize(thread);
    thread->entry=entry;
    thread->argument=argument;
    thread->id=next_thread_id++;
    thread->state=THREAD_READY;
    thread_context_initialize(thread);
    increment_saturated(&counters.threads_created);
    increment_saturated(&counters.active_threads);
    if (thread_id!=NULL) *thread_id=thread->id;
    restore_interrupt_flag(saved_flags);
    return true;
}
bool scheduler_run_until_idle(void) {
    const uint64_t saved_flags=read_rflags();
    struct kernel_thread *next=NULL;
    if (!initialized || scheduler_running ||
        current_thread!=&bootstrap_thread ||
        bootstrap_thread.state!=THREAD_RUNNING) return false;
    disable_interrupts();
    scheduler_running=true;
    bootstrap_thread.state=THREAD_READY;
    next=find_ready_thread(NULL);
    if (next==NULL) {
        bootstrap_thread.state=THREAD_RUNNING;
        scheduler_running=false;
        restore_interrupt_flag(saved_flags);
        return true;
    }
    next->state=THREAD_RUNNING;
    switch_to_thread(next);
    /* Bootstrap resumes here after all worker threads exit. */
    if (current_thread!=&bootstrap_thread ||
        bootstrap_thread.state!=THREAD_RUNNING) cpu_halt_forever();
    scheduler_running=false;
    restore_interrupt_flag(saved_flags);
    return true;
}
void kernel_thread_yield(void) {
    const uint64_t saved_flags=read_rflags();
    struct kernel_thread *self=current_thread;
    struct kernel_thread *next=NULL;
    if (!initialized || !scheduler_running || self==NULL ||
        self==&bootstrap_thread || self->state!=THREAD_RUNNING) return;
    disable_interrupts();
    if (!thread_guard_valid(self)) cpu_halt_forever();
    self->state=THREAD_READY;
    increment_saturated(&counters.voluntary_yields);
    next=find_ready_thread(self);
    if (next==NULL) {
        self->state=THREAD_RUNNING;
        restore_interrupt_flag(saved_flags);
        return;
    }
    next->state=THREAD_RUNNING;
    switch_to_thread(next);
    restore_interrupt_flag(saved_flags);
}
_Noreturn void kernel_thread_exit(void) {
    struct kernel_thread *self=current_thread;
    struct kernel_thread *next=NULL;
    disable_interrupts();
    if (!initialized || !scheduler_running || self==NULL ||
        self==&bootstrap_thread || self->state!=THREAD_RUNNING ||
        !thread_guard_valid(self)) cpu_halt_forever();
    self->state=THREAD_DEAD;
    increment_saturated(&counters.threads_exited);
    if (counters.active_threads>0ULL) --counters.active_threads;
    next=find_ready_thread(self);
    if (next!=NULL) {
        next->state=THREAD_RUNNING;
    } else if (bootstrap_thread.state==THREAD_READY) {
        bootstrap_thread.state=THREAD_RUNNING;
        next=&bootstrap_thread;
    } else {
        cpu_halt_forever();
    }
    switch_to_thread(next);
    cpu_halt_forever();
}
void scheduler_get_stats(struct scheduler_stats *stats) {
    uint64_t saved_flags=0ULL;
    if (stats==NULL || !initialized) return;
    saved_flags=read_rflags();
    disable_interrupts();
    *stats=counters;
    restore_interrupt_flag(saved_flags);
}
#ifdef DAWN_KERNEL_TEST
static volatile uint64_t scheduler_test_counts[2];
static void scheduler_test_worker(void *argument) {
    const uintptr_t worker_index=(uintptr_t)argument;
    if (worker_index>=2U) cpu_halt_forever();
    for (uint64_t iteration=0ULL;iteration<4ULL;iteration++) {
        ++scheduler_test_counts[worker_index];
        kernel_thread_yield();
    }
}
bool scheduler_self_test(void) {
    struct scheduler_stats stats={0};
    uint64_t first_id=0ULL,second_id=0ULL;
    if (!initialized || current_thread!=&bootstrap_thread || scheduler_running)
        return false;
    scheduler_test_counts[0]=0ULL;
    scheduler_test_counts[1]=0ULL;
    if (!kernel_thread_create(scheduler_test_worker,(void *)(uintptr_t)0U,&first_id) ||
        !kernel_thread_create(scheduler_test_worker,(void *)(uintptr_t)1U,&second_id) ||
        first_id==0ULL || second_id==0ULL || first_id==second_id ||
        !scheduler_run_until_idle()) return false;
    scheduler_get_stats(&stats);
    if (scheduler_test_counts[0]!=4ULL || scheduler_test_counts[1]!=4ULL ||
        stats.threads_created!=2ULL || stats.threads_exited!=2ULL ||
        stats.active_threads!=0ULL || stats.voluntary_yields<8ULL ||
        stats.context_switches<4ULL) return false;
    for (size_t i=0U;i<KERNEL_THREAD_MAX;i++) {
        if (thread_pool[i].state==THREAD_DEAD &&
            !thread_guard_valid(&thread_pool[i])) return false;
    }
    return true;
}
#endif
