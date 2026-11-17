/* SPDX-License-Identifier: Unlicense */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "interrupts.h"
#include "scheduler.h"

#define KERNEL_THREAD_MAX 8U
#define KERNEL_THREAD_STACK_SIZE (16U * 1024U)
#define KERNEL_THREAD_GUARD_SIZE 64U
#define KERNEL_THREAD_GUARD_BYTE 0xa5U
#define CPU_RFLAGS_INTERRUPT_ENABLE (1ULL << 9)
#define INITIAL_THREAD_RFLAGS 0x202ULL

enum thread_state { THREAD_FREE=0, THREAD_READY, THREAD_RUNNING, THREAD_DEAD };

struct kernel_thread {
    struct interrupt_frame *saved_frame;
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
static uint16_t kernel_code_selector;
static bool initialized;
static bool scheduler_running;
static struct scheduler_stats counters;

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
static uint16_t read_cs(void) {
    uint16_t value=0U;
    __asm__ volatile ("mov %%cs, %0" : "=r"(value));
    return value;
}
static _Noreturn void cpu_halt_forever(void) {
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
static _Noreturn void kernel_thread_bootstrap(void);
static void thread_frame_initialize(struct kernel_thread *thread) {
    uintptr_t stack_end=0U;
    uintptr_t initial_rsp=0U;
    struct interrupt_frame *frame=NULL;
    if (thread==NULL) return;

    stack_end=(uintptr_t)(thread->stack+KERNEL_THREAD_STACK_SIZE);
    /* IRETQ consumes RIP/CS/RFLAGS only for same-CPL returns. Leave the
     * post-IRET stack at SysV function-entry alignment (RSP % 16 == 8). */
    initial_rsp=(stack_end&~(uintptr_t)0xfU)-8U;
    frame=(struct interrupt_frame *)(uintptr_t)(initial_rsp-sizeof(*frame));
    if ((uintptr_t)frame<(uintptr_t)thread->stack+KERNEL_THREAD_GUARD_SIZE)
        cpu_halt_forever();

    *frame=(struct interrupt_frame){0};
    frame->rip=(uint64_t)(uintptr_t)kernel_thread_bootstrap;
    frame->cs=(uint64_t)kernel_code_selector;
    frame->rflags=INITIAL_THREAD_RFLAGS;
    thread->saved_frame=frame;
}
static bool thread_frame_valid(const struct kernel_thread *thread,
                               const struct interrupt_frame *frame) {
    uintptr_t frame_address=0U;
    uintptr_t stack_begin=0U;
    uintptr_t stack_end=0U;
    if (thread==NULL || frame==NULL) return false;
    frame_address=(uintptr_t)frame;
    stack_begin=(uintptr_t)thread->stack+KERNEL_THREAD_GUARD_SIZE;
    stack_end=(uintptr_t)(thread->stack+KERNEL_THREAD_STACK_SIZE);
    return (frame_address&0x7U)==0U && frame_address>=stack_begin &&
           frame_address<=stack_end-sizeof(*frame);
}

static bool scheduler_dispatch_state_valid(
    const struct kernel_thread *self,bool timer_interrupt
) {
    uint32_t running_count=0U;
    for (size_t i=0U;i<KERNEL_THREAD_MAX;i++) {
        const struct kernel_thread *candidate=&thread_pool[i];
        if (candidate->state==THREAD_RUNNING) {
            ++running_count;
            if (candidate!=self) return false;
        }
    }
    if (timer_interrupt)
        return self!=&bootstrap_thread && self->state==THREAD_RUNNING &&
               running_count==1U;
    return running_count==0U;
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

static _Noreturn void kernel_thread_bootstrap(void) {
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
    kernel_code_selector=read_cs();
    if (kernel_code_selector==0U) {
        restore_interrupt_flag(saved_flags);
        return false;
    }
    for (size_t i=0U;i<KERNEL_THREAD_MAX;i++) {
        thread_pool[i].saved_frame=NULL;
        thread_pool[i].entry=NULL;
        thread_pool[i].argument=NULL;
        thread_pool[i].id=0ULL;
        thread_pool[i].state=THREAD_FREE;
    }
    bootstrap_thread.saved_frame=NULL;
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
    thread_frame_initialize(thread);
    increment_saturated(&counters.threads_created);
    increment_saturated(&counters.active_threads);
    if (thread_id!=NULL) *thread_id=thread->id;
    restore_interrupt_flag(saved_flags);
    return true;
}

struct interrupt_frame *scheduler_handle_interrupt(
    struct interrupt_frame *frame,bool timer_interrupt
) {
    struct kernel_thread *self=current_thread;
    struct kernel_thread *next=NULL;
    if (frame==NULL || !initialized || !scheduler_running) return frame;
    if (self==NULL || self->state==THREAD_FREE ||
        (self!=&bootstrap_thread && !thread_guard_valid(self)))
        cpu_halt_forever();

    /* A hardware tick may interrupt only the currently running worker.
     * Software reschedule is issued only after READY/DEAD is committed. */
    if (timer_interrupt) {
        if (self==&bootstrap_thread || self->state!=THREAD_RUNNING)
            cpu_halt_forever();
    } else if ((self==&bootstrap_thread && self->state!=THREAD_READY) ||
               (self!=&bootstrap_thread && self->state!=THREAD_READY &&
                self->state!=THREAD_DEAD)) {
        cpu_halt_forever();
    }

    if (self!=&bootstrap_thread && !thread_frame_valid(self,frame))
        cpu_halt_forever();
    if (!scheduler_dispatch_state_valid(self,timer_interrupt))
        cpu_halt_forever();

    self->saved_frame=frame;
    if (timer_interrupt) self->state=THREAD_READY;

    next=find_ready_thread(self);
    if (next==NULL) {
        if (self->state==THREAD_DEAD) {
            /* The bootstrap frame is the completion return path after the
             * final worker exits. It is intentionally not in the worker pool. */
            if (self==&bootstrap_thread ||
                bootstrap_thread.state!=THREAD_READY ||
                bootstrap_thread.saved_frame==NULL)
                cpu_halt_forever();
            bootstrap_thread.state=THREAD_RUNNING;
            current_thread=&bootstrap_thread;
            increment_saturated(&counters.context_switches);
            return bootstrap_thread.saved_frame;
        }
        if (self->state==THREAD_READY)
            self->state=THREAD_RUNNING;
        return frame;
    }

    if (next->saved_frame==NULL || !thread_guard_valid(next) ||
        !thread_frame_valid(next,next->saved_frame))
        cpu_halt_forever();
    if (timer_interrupt) increment_saturated(&counters.timer_preemptions);
    if (self->state==THREAD_RUNNING)
        self->state=THREAD_READY;
    next->state=THREAD_RUNNING;
    current_thread=next;
    increment_saturated(&counters.context_switches);
    return next->saved_frame;
}

bool scheduler_run_until_idle(void) {
    const uint64_t saved_flags=read_rflags();
    if (!initialized || scheduler_running ||
        current_thread!=&bootstrap_thread ||
        bootstrap_thread.state!=THREAD_RUNNING) return false;
    disable_interrupts();
    scheduler_running=true;
    bootstrap_thread.state=THREAD_READY;
    /* Vector 0x42 is a software reschedule request; it does not send APIC EOI. */
    __asm__ volatile ("int $0x42" : : : "memory");
    if (current_thread!=&bootstrap_thread ||
        bootstrap_thread.state!=THREAD_RUNNING) cpu_halt_forever();
    scheduler_running=false;
    restore_interrupt_flag(saved_flags);
    return true;
}

void kernel_thread_yield(void) {
    const uint64_t saved_flags=read_rflags();
    struct kernel_thread *self=current_thread;
    if (!initialized || !scheduler_running || self==NULL ||
        self==&bootstrap_thread || self->state!=THREAD_RUNNING) return;
    disable_interrupts();
    if (!thread_guard_valid(self)) cpu_halt_forever();
    self->state=THREAD_READY;
    increment_saturated(&counters.voluntary_yields);
    __asm__ volatile ("int $0x42" : : : "memory");
    restore_interrupt_flag(saved_flags);
}

_Noreturn void kernel_thread_exit(void) {
    struct kernel_thread *self=current_thread;
    disable_interrupts();
    if (!initialized || !scheduler_running || self==NULL ||
        self==&bootstrap_thread || self->state!=THREAD_RUNNING ||
        !thread_guard_valid(self)) cpu_halt_forever();
    self->state=THREAD_DEAD;
    increment_saturated(&counters.threads_exited);
    if (counters.active_threads>0ULL) --counters.active_threads;
    __asm__ volatile ("int $0x42" : : : "memory");
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
static volatile uint64_t preemption_peer_progress;
static volatile uint64_t preemption_peer_observed;
static volatile uint64_t preemption_wait_expired;
static volatile uint64_t preemption_stress_started_mask;
static volatile uint64_t preemption_stress_timeout_mask;

static void scheduler_test_worker(void *argument) {
    const uintptr_t worker_index=(uintptr_t)argument;
    if (worker_index>=2U) cpu_halt_forever();
    for (uint64_t iteration=0ULL;iteration<4ULL;iteration++) {
        ++scheduler_test_counts[worker_index];
        kernel_thread_yield();
    }
}
static void scheduler_test_noop_worker(void *argument) {
    (void)argument;
}
static void scheduler_preemption_waiter(void *argument) {
    (void)argument;
    for (uint64_t attempt=0ULL;attempt<50000000ULL;attempt++) {
        if (preemption_peer_progress!=0ULL) {
            preemption_peer_observed=1ULL;
            return;
        }
    }
    preemption_wait_expired=1ULL;
}
static void scheduler_preemption_peer(void *argument) {
    (void)argument;
    preemption_peer_progress=1ULL;
}
static void scheduler_preemption_stress_worker(void *argument) {
    const uintptr_t index=(uintptr_t)argument;
    uint64_t bit=0ULL;
    if (index>=KERNEL_THREAD_MAX) cpu_halt_forever();
    bit=1ULL<<(uint32_t)index;
    preemption_stress_started_mask|=bit;
    for (uint64_t attempt=0ULL;attempt<50000000ULL;attempt++) {
        if (preemption_stress_started_mask==((1ULL<<KERNEL_THREAD_MAX)-1ULL))
            return;
    }
    preemption_stress_timeout_mask|=bit;
}

bool scheduler_self_test(void) {
    struct scheduler_stats stats={0};
    uint64_t first_id=0ULL,second_id=0ULL;
    uint64_t capacity_ids[KERNEL_THREAD_MAX]={0ULL};
    uint64_t reused_id=0ULL;
    if (!initialized || current_thread!=&bootstrap_thread || scheduler_running ||
        kernel_thread_create(NULL,NULL,NULL)) return false;
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

    /* Fill every slot, reject overflow, then verify DEAD slots can be reused. */
    for (size_t i=0U;i<KERNEL_THREAD_MAX;i++) {
        if (!kernel_thread_create(scheduler_test_noop_worker,NULL,
                                  &capacity_ids[i]) ||
            capacity_ids[i]==0ULL) return false;
        for (size_t previous=0U;previous<i;previous++) {
            if (capacity_ids[previous]==capacity_ids[i]) return false;
        }
    }
    if (kernel_thread_create(scheduler_test_noop_worker,NULL,NULL) ||
        !scheduler_run_until_idle()) return false;
    scheduler_get_stats(&stats);
    if (stats.threads_created!=10ULL || stats.threads_exited!=10ULL ||
        stats.active_threads!=0ULL) return false;

    if (!kernel_thread_create(scheduler_test_noop_worker,NULL,&reused_id) ||
        reused_id==0ULL || reused_id<=capacity_ids[KERNEL_THREAD_MAX-1U] ||
        !scheduler_run_until_idle()) return false;
    scheduler_get_stats(&stats);
    if (stats.threads_created!=11ULL || stats.threads_exited!=11ULL ||
        stats.active_threads!=0ULL) return false;
    return true;
}

bool scheduler_preemption_self_test(void) {
    struct scheduler_stats before={0};
    struct scheduler_stats after={0};
    const uint64_t saved_flags=read_rflags();
    if (!initialized || current_thread!=&bootstrap_thread || scheduler_running)
        return false;
    scheduler_get_stats(&before);

    /* Repeat to exercise DEAD-slot reuse and a worker exiting after its peer
     * has been preempted by the periodic timer. */
    for (uint32_t round=0U;round<4U;round++) {
        disable_interrupts();
        ready_cursor=0ULL;
        preemption_peer_progress=0ULL;
        preemption_peer_observed=0ULL;
        preemption_wait_expired=0ULL;
        restore_interrupt_flag(saved_flags);
        if (!kernel_thread_create(scheduler_preemption_waiter,NULL,NULL) ||
            !kernel_thread_create(scheduler_preemption_peer,NULL,NULL) ||
            !scheduler_run_until_idle()) return false;
        if (preemption_peer_observed!=1ULL ||
            preemption_wait_expired!=0ULL ||
            preemption_peer_progress!=1ULL) return false;
        scheduler_get_stats(&after);
        if (after.active_threads!=0ULL) return false;
        for (size_t slot=0U;slot<KERNEL_THREAD_MAX;slot++) {
            if (thread_pool[slot].state==THREAD_DEAD &&
                (!thread_guard_valid(&thread_pool[slot]) ||
                 !thread_frame_valid(&thread_pool[slot],
                                     thread_pool[slot].saved_frame)))
                return false;
        }
    }

    scheduler_get_stats(&after);
    if (after.threads_exited<before.threads_exited+8ULL ||
        after.context_switches<before.context_switches+16ULL ||
        after.timer_preemptions<before.timer_preemptions+4ULL ||
        after.active_threads!=0ULL) return false;

    /* Eight non-yielding workers must all start before any can finish. Only
     * periodic timer preemption can advance the run queue across this barrier. */
    disable_interrupts();
    ready_cursor=0ULL;
    preemption_stress_started_mask=0ULL;
    preemption_stress_timeout_mask=0ULL;
    restore_interrupt_flag(saved_flags);
    scheduler_get_stats(&before);
    for (uintptr_t index=0U;index<KERNEL_THREAD_MAX;index++) {
        if (!kernel_thread_create(scheduler_preemption_stress_worker,
                                  (void *)index,NULL)) return false;
    }
    if (!scheduler_run_until_idle()) return false;
    scheduler_get_stats(&after);
    if (preemption_stress_started_mask!=((1ULL<<KERNEL_THREAD_MAX)-1ULL) ||
        preemption_stress_timeout_mask!=0ULL || after.active_threads!=0ULL ||
        after.threads_exited<before.threads_exited+KERNEL_THREAD_MAX ||
        after.timer_preemptions<before.timer_preemptions+(KERNEL_THREAD_MAX-1ULL))
        return false;
    for (size_t slot=0U;slot<KERNEL_THREAD_MAX;slot++) {
        if (!thread_guard_valid(&thread_pool[slot]) ||
            !thread_frame_valid(&thread_pool[slot],thread_pool[slot].saved_frame))
            return false;
    }
    return true;
}
#endif
